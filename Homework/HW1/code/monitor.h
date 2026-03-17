#ifndef MONITOR_H
#define MONITOR_H

#include <systemc.h>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>
#include <iostream>
#include <iomanip>

// ============================================================
// Monitor: receives softmax probabilities and prints Top-100
// ============================================================
SC_MODULE(Monitor) {

    std::string classes_file; // path to imagenet_classes.txt

    SC_CTOR(Monitor) {}

    // Load class names from file (one per line)
    std::vector<std::string> load_classes(const std::string& path) {
        std::vector<std::string> names;
        std::ifstream f(path);
        if (!f.is_open()) {
            std::cerr << "[ERROR] Cannot open classes file: " << path << std::endl;
            return names;
        }
        std::string line;
        while (std::getline(f, line)) {
            // Remove trailing \r if any
            if (!line.empty() && line.back() == '\r') line.pop_back();
            names.push_back(line);
        }
        return names;
    }

    // Print top-100 results in the required format
    void print_top100(const std::vector<float>& probs, const std::string& classes_path) {
        std::vector<std::string> class_names = load_classes(classes_path);

        // Build (prob * 100, index) pairs sorted descending
        std::vector<std::pair<float, int>> ranked;
        ranked.reserve(probs.size());
        for (int i = 0; i < (int)probs.size(); ++i)
            ranked.push_back({probs[i] * 100.0f, i});
        std::sort(ranked.begin(), ranked.end(),
                  [](const std::pair<float,int>& a, const std::pair<float,int>& b){
                      return a.first > b.first;
                  });

        // We also need raw logit value for "val" column.
        // Per assignment example, "val" = softmax input (logit).
        // However we only have probabilities here; the monitor will receive
        // raw fc8 output separately for val. We'll use log(prob) + offset as
        // approximation, but for correctness the main will pass both.
        // (See main.cpp for the actual usage)

        std::cout << std::fixed << std::setprecision(2);
        std::cout << "Top 100 classes:" << std::endl;
        std::cout << "=================================================" << std::endl;
        std::cout << std::right << std::setw(5) << "idx"
                  << " | " << std::setw(8) << "val"
                  << " | " << std::setw(11) << "possibility"
                  << " | " << "class name" << std::endl;
        std::cout << "-------------------------------------------------" << std::endl;

        for (int i = 0; i < 100 && i < (int)ranked.size(); ++i) {
            int   idx  = ranked[i].second;
            float poss = ranked[i].first;
            // val will be filled by the caller passing fc8 output
            std::cout << std::right << std::setw(5) << idx
                      << " | " << std::setw(8) << 0.0f  // placeholder, see print_top100_with_val
                      << " | " << std::setw(11) << poss
                      << " | " << (idx < (int)class_names.size() ? class_names[idx] : "unknown")
                      << std::endl;
        }
        std::cout << "=================================================" << std::endl;
    }

    // Full version: pass both fc8 logits and softmax probabilities
    void print_top100_with_val(const std::vector<float>& logits,
                               const std::vector<float>& probs,
                               const std::string& classes_path) {
        std::vector<std::string> class_names = load_classes(classes_path);

        // Build (prob*100, logit, index) sorted by prob descending
        std::vector<std::tuple<float, float, int>> ranked;
        ranked.reserve(probs.size());
        for (int i = 0; i < (int)probs.size(); ++i)
            ranked.push_back({probs[i] * 100.0f, logits[i], i});
        std::sort(ranked.begin(), ranked.end(),
                  [](const std::tuple<float,float,int>& a, const std::tuple<float,float,int>& b){
                      return std::get<0>(a) > std::get<0>(b);
                  });

        std::cout << std::fixed << std::setprecision(2);
        std::cout << "Top 100 classes:" << std::endl;
        std::cout << "=================================================" << std::endl;
        std::cout << std::right << std::setw(5) << "idx"
                  << " | " << std::setw(8) << "val"
                  << " | " << std::setw(11) << "possibility"
                  << " | " << "class name" << std::endl;
        std::cout << "-------------------------------------------------" << std::endl;

        for (int i = 0; i < 100 && i < (int)ranked.size(); ++i) {
            int   idx  = std::get<2>(ranked[i]);
            float val  = std::get<1>(ranked[i]);
            float poss = std::get<0>(ranked[i]);
            std::cout << std::right << std::setw(5) << idx
                      << " | " << std::setw(8) << val
                      << " | " << std::setw(11) << poss
                      << " | " << (idx < (int)class_names.size() ? class_names[idx] : "unknown")
                      << std::endl;
        }
        std::cout << "=================================================" << std::endl;
    }
};

#endif // MONITOR_H