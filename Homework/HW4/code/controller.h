#ifndef CONTROLLER_H
#define CONTROLLER_H

#include "systemc.h"
#include "pe.h"
#include <vector>
#include <queue>
#include <string>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
using namespace std;

SC_MODULE( Controller ) {
    sc_in  < bool >  rst;
    sc_in  < bool >  clk;

    // ROM interface
    sc_out < int >   layer_id;
    sc_out < bool >  layer_id_type;
    sc_out < bool >  layer_id_valid;
    sc_in  < float > data;
    sc_in  < bool >  data_valid;

    // Router LOCAL port
    sc_out < sc_lv<34> > flit_tx;
    sc_out < bool >      req_tx;
    sc_in  < bool >      ack_tx;

    sc_in  < sc_lv<34> > flit_rx;
    sc_in  < bool >      req_rx;
    sc_out < bool >      ack_rx;

    int id;

    void send_flit(sc_lv<34> f) {
        req_tx.write(1);
        flit_tx.write(f);
        while (ack_tx.read() == 0) wait();
        req_tx.write(0);
        while (ack_tx.read() == 1) wait();
    }

    void send_packet(int dest, const vector<float>& datas) {
        sc_lv<34> h;
        h.range(33,32) = 2;
        h.range(31,16) = dest;
        h.range(15, 0) = id;
        send_flit(h);
        for (size_t i = 0; i < datas.size(); i++) {
            sc_lv<34> f;
            f.range(33,32) = (i == datas.size()-1) ? 1 : 0;
            union { float fv; unsigned int iv; } cv;
            cv.fv = datas[i];
            f.range(31,0) = cv.iv;
            send_flit(f);
        }
    }

    Packet* recv_packet() {
        Packet* p = NULL;
        while (true) {
            while (req_rx.read() == 0) wait();
            sc_lv<34> f = flit_rx.read();
            ack_rx.write(1);
            while (req_rx.read() == 1) wait();
            ack_rx.write(0);

            unsigned int type = f.range(33,32).to_uint();
            if (type == 2) {
                p = new Packet();
                p->dest_id   = f.range(31,16).to_uint();
                p->source_id = f.range(15, 0).to_uint();
            } else if ((type == 0 || type == 1) && p) {
                union { float fv; unsigned int iv; } cv;
                cv.iv = f.range(31,0).to_uint();
                p->datas.push_back(cv.fv);
                if (type == 1) return p;
            }
            wait();
        }
        return NULL;
    }

    // Read one layer from ROM.
    // ROM protocol:
    //   Cycle N  : assert layer_id_valid for exactly 1 cycle
    //   Cycle N+1: ROM opens file, begins outputting data_valid=1 + data
    //   Each cycle data_valid==1: one float is valid on data port
    //   When EOF : ROM drops data_valid to 0
    vector<float> read_from_rom(int lid, bool ltype) {
        vector<float> buf;
        layer_id.write(lid);
        layer_id_type.write(ltype);
        layer_id_valid.write(true);
        wait();                       // assert for 1 cycle
        layer_id_valid.write(false);
        wait();                       // let ROM react

        while (true) {
            if (data_valid.read()) {
                buf.push_back(data.read());
                wait();
            } else {
                break;
            }
        }
        return buf;
    }

    void print_results(const vector<float>& linear_out,
                       const vector<float>& softmax_out) {
        // Load class names from imagenet_classes.txt (one class name per line)
        vector<string> class_names;
        ifstream lf("./data/imagenet_classes.txt");
        if (!lf.is_open())
            cerr << "[Controller] WARNING: imagenet_classes.txt not found!" << endl;
        string line;
        while (getline(lf, line)) {
            // Remove trailing \r if on Windows-style line endings
            if (!line.empty() && line.back() == '\r') line.pop_back();
            class_names.push_back(line);
        }
        while ((int)class_names.size() < 1000) class_names.push_back("unknown");

        // Sort top 100 by softmax
        vector<pair<float,int>> scored(1000);
        for (int i = 0; i < 1000; i++) scored[i] = {softmax_out[i], i};
        sort(scored.begin(), scored.end(),
             [](const pair<float,int>& a, const pair<float,int>& b){ return a.first > b.first; });

        cout << fixed << setprecision(2);
        cout << "Top 100 classes:" << endl;
        cout << "=================================================" << endl;
        cout << right << setw(5) << "idx"
             << " | " << setw(8) << "val"
             << " | " << setw(11) << "possibility"
             << " | " << "class name" << endl;
        cout << "-------------------------------------------------" << endl;
        for (int i = 0; i < 100; i++) {
            int idx  = scored[i].second;
            float val  = linear_out[idx];
            float poss = softmax_out[idx] * 100.0f;
            cout << right << setw(5) << idx
                 << " | " << setw(8) << val
                 << " | " << setw(11) << poss
                 << " | " << class_names[idx] << endl;
        }
        cout << "=================================================" << endl;
    }

    void run() {
        layer_id_valid.write(false);
        layer_id.write(0);
        layer_id_type.write(false);
        req_tx.write(false);
        flit_tx.write(0);
        ack_rx.write(false);

        // Wait for reset
        while (rst.read()) wait();
        wait();

        cout << "[Controller] Reading image from ROM..." << endl;
        vector<float> image = read_from_rom(0, false);
        cout << "[Controller] Image floats: " << image.size() << endl;

        cout << "[Controller] Sending image to Core 1 via NoC..." << endl;
        send_packet(1, image);
        cout << "[Controller] Image sent. Waiting for result..." << endl;

        Packet* result = recv_packet();
        cout << "[Controller] Result received (" << result->datas.size() << " floats)." << endl;

        vector<float> linear_out(1000), softmax_out(1000);
        for (int i = 0; i < 1000; i++) linear_out[i]  = result->datas[i];
        for (int i = 0; i < 1000; i++) softmax_out[i] = result->datas[1000+i];
        delete result;

        print_results(linear_out, softmax_out);
        sc_stop();
    }

    SC_HAS_PROCESS(Controller);
    Controller(sc_module_name name) : sc_module(name), id(0) {
        SC_THREAD(run);
        sensitive << clk.pos();
    }
};

#endif