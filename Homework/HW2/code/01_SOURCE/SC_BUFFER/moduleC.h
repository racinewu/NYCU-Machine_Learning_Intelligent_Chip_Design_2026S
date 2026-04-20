#ifndef MODULE_C_H
#define MODULE_C_H

#include "layers.h"
#include <systemc.h>

// ============================================================
// Module C: FC6+ReLU -> FC7+ReLU -> FC8 -> Softmax
//
//   Ports in  : clock, rst, in_valid, in_data (FlatSignal [9216])
//   Ports out : out_valid
//               output_softmax[NUM_CLASSES]
//               output_linear[NUM_CLASSES]
// ============================================================
SC_MODULE(ModuleC) {
    // --- Ports ---
    sc_in_clk                  clock;
    sc_in<bool>                rst;
    sc_in<bool>                in_valid;
    sc_in<FlatSignal>          in_data;

    sc_out<bool>               out_valid;
    sc_vector<sc_out<double>>  output_softmax{"output_softmax", 1000};
    sc_vector<sc_out<double>>  output_linear {"output_linear",  1000};

    // --- Internal computation ---
    FCLayerImpl      fc6;
    FCLayerImpl      fc7;
    FCLayerImpl      fc8;
    SoftmaxLayerImpl softmax;

    void load_weights(const std::string& dir) {
        // FC6: 9216 -> 4096 + ReLU
        fc6.in_features  = 9216;
        fc6.out_features = 4096;
        fc6.use_relu     = true;
        fc6.load_weights(dir + "/fc6_weight.txt", dir + "/fc6_bias.txt");

        // FC7: 4096 -> 4096 + ReLU
        fc7.in_features  = 4096;
        fc7.out_features = 4096;
        fc7.use_relu     = true;
        fc7.load_weights(dir + "/fc7_weight.txt", dir + "/fc7_bias.txt");

        // FC8: 4096 -> 1000, no ReLU
        fc8.in_features  = 4096;
        fc8.out_features = 1000;
        fc8.use_relu     = false;
        fc8.load_weights(dir + "/fc8_weight.txt", dir + "/fc8_bias.txt");

        softmax.size = 1000;
    }

    void run() {
        if (rst.read()) {
            out_valid.write(false);
            return;
        }

        if (in_valid.read()) {
            const std::vector<float>& flat = in_data.read().data;

            fc6.process(flat);
            fc7.process(fc6.output);
            fc8.process(fc7.output);
            softmax.process(fc8.output);

            for (int i = 0; i < 1000; ++i) {
                output_softmax[i].write(static_cast<double>(softmax.output[i]));
                output_linear[i].write(static_cast<double>(fc8.output[i]));
            }
            out_valid.write(true);
        } else {
            out_valid.write(false);
        }
    }

    SC_CTOR(ModuleC) {
        SC_METHOD(run);
        sensitive << clock.pos();
    }
};

#endif // MODULE_C_H