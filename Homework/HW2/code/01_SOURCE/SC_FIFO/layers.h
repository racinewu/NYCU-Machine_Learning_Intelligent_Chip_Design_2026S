#ifndef LAYERS_H
#define LAYERS_H

// ============================================================
// Step 1: STL only (no systemc.h yet)
// ============================================================
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <iostream>

// ============================================================
// Step 2: Type alias
// ============================================================
using FeatureMap = std::vector<std::vector<std::vector<float>>>;

// ============================================================
// Step 3: Wrapper structs with operator<<
//   Must be defined BEFORE systemc.h is included,
//   so sc_signal<T> instantiation can find operator<<.
// ============================================================
struct FeatureMapSignal {
    FeatureMap data;

    bool operator==(const FeatureMapSignal&) const { return false; }

    friend std::ostream& operator<<(std::ostream& os, const FeatureMapSignal&) {
        return os << "[FeatureMap]";
    }
};

struct FlatSignal {
    std::vector<float> data;

    bool operator==(const FlatSignal&) const { return false; }

    friend std::ostream& operator<<(std::ostream& os, const FlatSignal&) {
        return os << "[FlatSignal]";
    }
};

// ============================================================
// Step 4: Now include systemc.h
//   operator<< is already visible, so sc_signal instantiation works.
// ============================================================
#include <systemc.h>

// ============================================================
// Step 5: sc_trace stubs
//   sc_trace_file is defined inside systemc.h, so must come after.
//   These empty stubs satisfy sc_in<T>::end_of_elaboration().
// ============================================================
namespace sc_core {
    inline void sc_trace(sc_trace_file*, const FeatureMapSignal&, const std::string&) {}
    inline void sc_trace(sc_trace_file*, const FlatSignal&, const std::string&) {}
}

// ============================================================
// Step 6: Computation Impl structs
// ============================================================

// --- Utility ---
inline std::vector<float> load_txt(const std::string& path) {
    std::vector<float> result;
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "[ERROR] Cannot open: " << path << std::endl;
        return result;
    }
    float v;
    while (f >> v) result.push_back(v);
    return result;
}

// --- InputLayerImpl ---
struct InputLayerImpl {
    FeatureMap output;

    InputLayerImpl()
        : output(3,
                 std::vector<std::vector<float>>(227,
                 std::vector<float>(227, 0.0f))) {}

    void load_data_and_pad(const std::vector<double>& raw) {
        for (auto& ch : output)
            for (auto& row : ch)
                std::fill(row.begin(), row.end(), 0.0f);

        for (int c = 0; c < 3; ++c)
            for (int r = 0; r < 224; ++r)
                for (int col = 0; col < 224; ++col)
                    output[c][r + 2][col + 2] =
                        static_cast<float>(raw[c * 224 * 224 + r * 224 + col]);
    }
};

// --- ConvLayerImpl ---
struct ConvLayerImpl {
    int in_channels  = 0;
    int out_channels = 0;
    int kernel_size  = 0;
    int stride       = 1;
    int padding      = 0;
    int in_h         = 0;
    int in_w         = 0;

    std::vector<float> weights;
    std::vector<float> biases;
    FeatureMap output;

    void load_weights(const std::string& wpath, const std::string& bpath) {
        weights = load_txt(wpath);
        biases  = load_txt(bpath);
    }

    void process(const FeatureMap& input) {
        int out_h = (in_h + 2 * padding - kernel_size) / stride + 1;
        int out_w = (in_w + 2 * padding - kernel_size) / stride + 1;

        int pad_h = in_h + 2 * padding;
        int pad_w = in_w + 2 * padding;
        FeatureMap padded(in_channels,
            std::vector<std::vector<float>>(pad_h, std::vector<float>(pad_w, 0.0f)));
        for (int c = 0; c < in_channels; ++c)
            for (int r = 0; r < in_h; ++r)
                for (int col = 0; col < in_w; ++col)
                    padded[c][r + padding][col + padding] = input[c][r][col];

        output.assign(out_channels,
            std::vector<std::vector<float>>(out_h, std::vector<float>(out_w, 0.0f)));

        int k2 = kernel_size * kernel_size;
        for (int oc = 0; oc < out_channels; ++oc)
            for (int oh = 0; oh < out_h; ++oh)
                for (int ow = 0; ow < out_w; ++ow) {
                    float sum = biases[oc];
                    for (int ic = 0; ic < in_channels; ++ic) {
                        int w_base = ((oc * in_channels) + ic) * k2;
                        for (int kh = 0; kh < kernel_size; ++kh)
                            for (int kw = 0; kw < kernel_size; ++kw)
                                sum += padded[ic][oh * stride + kh][ow * stride + kw]
                                     * weights[w_base + kh * kernel_size + kw];
                    }
                    output[oc][oh][ow] = (sum > 0.0f) ? sum : 0.0f;
                }
    }
};

// --- MaxPoolLayerImpl ---
struct MaxPoolLayerImpl {
    int channels  = 0;
    int pool_size = 0;
    int stride    = 1;
    int in_h      = 0;
    int in_w      = 0;
    FeatureMap output;

    void process(const FeatureMap& input) {
        int out_h = (in_h - pool_size) / stride + 1;
        int out_w = (in_w - pool_size) / stride + 1;
        output.assign(channels,
            std::vector<std::vector<float>>(out_h, std::vector<float>(out_w, -1e30f)));

        for (int c = 0; c < channels; ++c)
            for (int oh = 0; oh < out_h; ++oh)
                for (int ow = 0; ow < out_w; ++ow) {
                    float mx = -1e30f;
                    for (int kh = 0; kh < pool_size; ++kh)
                        for (int kw = 0; kw < pool_size; ++kw)
                            mx = std::max(mx, input[c][oh * stride + kh][ow * stride + kw]);
                    output[c][oh][ow] = mx;
                }
    }
};

// --- FCLayerImpl ---
struct FCLayerImpl {
    int  in_features  = 0;
    int  out_features = 0;
    bool use_relu     = true;

    std::vector<float> weights;
    std::vector<float> biases;
    std::vector<float> output;

    void load_weights(const std::string& wpath, const std::string& bpath) {
        weights = load_txt(wpath);
        biases  = load_txt(bpath);
    }

    void process(const std::vector<float>& input) {
        output.assign(out_features, 0.0f);
        for (int o = 0; o < out_features; ++o) {
            float sum = biases[o];
            for (int i = 0; i < in_features; ++i)
                sum += input[i] * weights[o * in_features + i];
            output[o] = (use_relu && sum < 0.0f) ? 0.0f : sum;
        }
    }
};

// --- SoftmaxLayerImpl ---
struct SoftmaxLayerImpl {
    int size = 1000;
    std::vector<float> output;

    void process(const std::vector<float>& input) {
        output.resize(size);
        float mx = *std::max_element(input.begin(), input.end());
        float sum = 0.0f;
        for (int i = 0; i < size; ++i) {
            output[i] = std::exp(input[i] - mx);
            sum += output[i];
        }
        for (int i = 0; i < size; ++i) output[i] /= sum;
    }
};

#endif // LAYERS_H