#ifndef CORE_H
#define CORE_H

#include "systemc.h"
#include "pe.h"
#include "layers.h"
#include <vector>
#include <queue>
#include <string>
using namespace std;

// ============================================================
// Core ID assignment:
//   0 = Controller (router 0 LOCAL)
//   1 = ModuleA PE (router 1 LOCAL) : Conv1 + Pool1
//   2 = ModuleB PE (router 2 LOCAL) : Conv2..Conv5 + Pool5
//   3 = ModuleC PE (router 3 LOCAL) : FC6, FC7, FC8, Softmax
//
// Packet flow:
//   Controller --[image floats]--> Core1
//   Core1       --[pool1 output]--> Core2
//   Core2       --[flat 9216]----> Core3
//   Core3       --[fc8+softmax]---> Controller
// ============================================================

SC_MODULE( Core ) {
    sc_in  < bool >  rst;
    sc_in  < bool >  clk;

    sc_in  < sc_lv<34> > flit_rx;
    sc_in  < bool >      req_rx;
    sc_out < bool >      ack_rx;

    sc_out < sc_lv<34> > flit_tx;
    sc_out < bool >      req_tx;
    sc_in  < bool >      ack_tx;

    int core_id;
    string data_dir;

    // --- AlexNet layer impls ---
    // Core 1: ModuleA
    InputLayerImpl   input_layer;
    ConvLayerImpl    conv1;
    MaxPoolLayerImpl pool1;

    // Core 2: ModuleB
    ConvLayerImpl    conv2, conv3, conv4, conv5;
    MaxPoolLayerImpl pool2, pool5;

    // Core 3: ModuleC
    FCLayerImpl      fc6, fc7, fc8;
    SoftmaxLayerImpl softmax_layer;

    // --- TX queue ---
    std::queue<Packet*> tx_queue;

    void init(int id, const string& dir) {
        core_id  = id;
        data_dir = dir;
        setup_layers();
    }

    void setup_layers() {
        if (core_id == 1) {
            conv1.in_channels=3; conv1.out_channels=64; conv1.kernel_size=11;
            conv1.stride=4; conv1.padding=0; conv1.in_h=227; conv1.in_w=227;
            conv1.load_weights(data_dir+"/conv1_weight.txt", data_dir+"/conv1_bias.txt");
            pool1.channels=64; pool1.pool_size=3; pool1.stride=2; pool1.in_h=55; pool1.in_w=55;
        } else if (core_id == 2) {
            conv2.in_channels=64;  conv2.out_channels=192; conv2.kernel_size=5;
            conv2.stride=1; conv2.padding=2; conv2.in_h=27; conv2.in_w=27;
            conv2.load_weights(data_dir+"/conv2_weight.txt", data_dir+"/conv2_bias.txt");
            pool2.channels=192; pool2.pool_size=3; pool2.stride=2; pool2.in_h=27; pool2.in_w=27;

            conv3.in_channels=192; conv3.out_channels=384; conv3.kernel_size=3;
            conv3.stride=1; conv3.padding=1; conv3.in_h=13; conv3.in_w=13;
            conv3.load_weights(data_dir+"/conv3_weight.txt", data_dir+"/conv3_bias.txt");

            conv4.in_channels=384; conv4.out_channels=256; conv4.kernel_size=3;
            conv4.stride=1; conv4.padding=1; conv4.in_h=13; conv4.in_w=13;
            conv4.load_weights(data_dir+"/conv4_weight.txt", data_dir+"/conv4_bias.txt");

            conv5.in_channels=256; conv5.out_channels=256; conv5.kernel_size=3;
            conv5.stride=1; conv5.padding=1; conv5.in_h=13; conv5.in_w=13;
            conv5.load_weights(data_dir+"/conv5_weight.txt", data_dir+"/conv5_bias.txt");

            pool5.channels=256; pool5.pool_size=3; pool5.stride=2; pool5.in_h=13; pool5.in_w=13;
        } else if (core_id == 3) {
            fc6.in_features=9216; fc6.out_features=4096; fc6.use_relu=true;
            fc6.load_weights(data_dir+"/fc6_weight.txt", data_dir+"/fc6_bias.txt");
            fc7.in_features=4096; fc7.out_features=4096; fc7.use_relu=true;
            fc7.load_weights(data_dir+"/fc7_weight.txt", data_dir+"/fc7_bias.txt");
            fc8.in_features=4096; fc8.out_features=1000; fc8.use_relu=false;
            fc8.load_weights(data_dir+"/fc8_weight.txt", data_dir+"/fc8_bias.txt");
            softmax_layer.size = 1000;
        }
    }

    // --- Send a flit using 4-phase handshake ---
    void send_flit(sc_lv<34> f) {
        req_tx.write(1);
        flit_tx.write(f);
        while (ack_tx.read() == 0) wait();
        req_tx.write(0);
        while (ack_tx.read() == 1) wait();
    }

    void send_packet(Packet* p) {
        // Header
        sc_lv<34> h;
        h.range(33,32) = 2;
        h.range(31,16) = p->dest_id;
        h.range(15, 0) = p->source_id;
        send_flit(h);
        // Body/Tail
        for (size_t i=0; i<p->datas.size(); i++) {
            sc_lv<34> f;
            f.range(33,32) = (i == p->datas.size()-1) ? 1 : 0;
            union { float fv; unsigned int iv; } cv;
            cv.fv = p->datas[i];
            f.range(31,0) = cv.iv;
            send_flit(f);
        }
    }

    void tx_thread() {
        req_tx.write(0);
        while (true) {
            while (tx_queue.empty()) wait();
            Packet* p = tx_queue.front();
            tx_queue.pop();
            send_packet(p);
            delete p;
        }
    }

    // Receive a full packet and return it
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
            } else if (type == 0 || type == 1) {
                union { float fv; unsigned int iv; } cv;
                cv.iv = f.range(31,0).to_uint();
                if (p) p->datas.push_back(cv.fv);
                if (type == 1 && p) return p;
            }
            wait();
        }
        return NULL;
    }

    void rx_thread() {
        ack_rx.write(0);
        while (true) {
            Packet* p = recv_packet();
            if (!p) { wait(); continue; }

            Packet* out = new Packet();

            if (core_id == 1) {
                // Input: 150528 floats (image)
                // Reconstruct vector<float>
                vector<float> raw(p->datas.begin(), p->datas.end());
                delete p;

                input_layer.load_data_and_pad(raw);
                conv1.process(input_layer.output);
                pool1.process(conv1.output);

                // Flatten [64][27][27] -> 46656 floats
                out->source_id = core_id;
                out->dest_id   = 2;
                out->datas.reserve(64*27*27);
                for (int c=0; c<64; c++)
                    for (int r=0; r<27; r++)
                        for (int col=0; col<27; col++)
                            out->datas.push_back(pool1.output[c][r][col]);

            } else if (core_id == 2) {
                // Input: 46656 floats [64][27][27]
                FeatureMap fm(64, vector<vector<float>>(27, vector<float>(27)));
                int idx=0;
                for (int c=0; c<64; c++)
                    for (int r=0; r<27; r++)
                        for (int col=0; col<27; col++)
                            fm[c][r][col] = p->datas[idx++];
                delete p;

                conv2.process(fm);
                pool2.process(conv2.output);
                conv3.process(pool2.output);
                conv4.process(conv3.output);
                conv5.process(conv4.output);
                pool5.process(conv5.output);

                // Flatten [256][6][6] -> 9216
                out->source_id = core_id;
                out->dest_id   = 3;
                out->datas.reserve(9216);
                for (int c=0; c<256; c++)
                    for (int r=0; r<6; r++)
                        for (int col=0; col<6; col++)
                            out->datas.push_back(pool5.output[c][r][col]);

            } else if (core_id == 3) {
                // Input: 9216 floats
                vector<float> flat(p->datas.begin(), p->datas.end());
                delete p;

                fc6.process(flat);
                fc7.process(fc6.output);
                fc8.process(fc7.output);
                softmax_layer.process(fc8.output);

                // Send fc8 (linear) + softmax back to controller (id=0)
                // Pack: [1000 linear] then [1000 softmax]
                out->source_id = core_id;
                out->dest_id   = 0;
                out->datas.reserve(2000);
                for (int i=0; i<1000; i++) out->datas.push_back(fc8.output[i]);
                for (int i=0; i<1000; i++) out->datas.push_back(softmax_layer.output[i]);
            } else {
                delete p;
                delete out;
                out = NULL;
            }

            if (out) tx_queue.push(out);
        }
    }

    SC_HAS_PROCESS(Core);
    Core(sc_module_name name) : sc_module(name), core_id(0) {
        SC_THREAD(tx_thread);
        sensitive << clk.pos();
        SC_THREAD(rx_thread);
        sensitive << clk.pos();
    }
};

#endif