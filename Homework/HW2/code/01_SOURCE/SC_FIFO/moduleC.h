#ifndef MODULE_C_H
#define MODULE_C_H

#include "layers.h"
#include <systemc.h>

// ============================================================
// Module C (FIFO version): FC6+ReLU -> FC7+ReLU -> FC8 -> Softmax
//
//   Ports in  : clock, rst, sc_fifo_in<FlatSignal>
//   Ports out : out_valid, output_softmax[NUM_CLASSES], output_linear[NUM_CLASSES]
//
//   SC_THREAD + blocking fifo.read()
//   不需要 in_valid
// ============================================================
SC_MODULE(ModuleC) {
    // --- Ports ---
    sc_in_clk                  clock;
    sc_in<bool>                rst;

    sc_fifo_in<FlatSignal>     in_data;

    sc_out<bool>               out_valid;
    sc_vector<sc_out<double>>  output_softmax{"output_softmax", 1000};
    sc_vector<sc_out<double>>  output_linear {"output_linear",  1000};

    // --- Internal computation ---
    FCLayerImpl      fc6;
    FCLayerImpl      fc7;
    FCLayerImpl      fc8;
    SoftmaxLayerImpl softmax;

    void load_weights(const std::string& dir) {
        fc6.in_features  = 9216;
        fc6.out_features = 4096;
        fc6.use_relu     = true;
        fc6.load_weights(dir + "/fc6_weight.txt", dir + "/fc6_bias.txt");

        fc7.in_features  = 4096;
        fc7.out_features = 4096;
        fc7.use_relu     = true;
        fc7.load_weights(dir + "/fc7_weight.txt", dir + "/fc7_bias.txt");

        fc8.in_features  = 4096;
        fc8.out_features = 1000;
        fc8.use_relu     = false;
        fc8.load_weights(dir + "/fc8_weight.txt", dir + "/fc8_bias.txt");

        softmax.size = 1000;
    }

    void run() {
        out_valid.write(false);

        while (true) {
            FlatSignal input_sig = in_data.read();

            if (rst.read()) continue;

            const std::vector<float>& flat = input_sig.data;

            fc6.process(flat);
            fc7.process(fc6.output);
            fc8.process(fc7.output);
            softmax.process(fc8.output);

            for (int i = 0; i < 1000; ++i) {
                output_softmax[i].write(static_cast<double>(softmax.output[i]));
                output_linear[i].write(static_cast<double>(fc8.output[i]));
            }
            out_valid.write(true);
            wait(clock.negedge_event());
            out_valid.write(false);
            return;
        }
    }

    SC_CTOR(ModuleC) {
        SC_THREAD(run);
    }
};

#endif // MODULE_C_H