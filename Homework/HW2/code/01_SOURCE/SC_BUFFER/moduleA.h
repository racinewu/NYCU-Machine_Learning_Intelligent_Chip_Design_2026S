#ifndef MODULE_A_H
#define MODULE_A_H

// layers.h must be included BEFORE systemc.h so that
// FeatureMapSignal::operator<< is defined before sc_buffer instantiation
#include "layers.h"
#include <systemc.h>

// ============================================================
// Module A: InputLayer (zero-pad) + Conv1 + ReLU + Pool1
//
//   Ports in  : clock, rst, in_valid, img[IMG_SIZE]
//   Ports out : out_data (FeatureMapSignal), out_valid
//
//   Output feature map: [64][27][27]
// ============================================================
SC_MODULE(ModuleA) {
    // --- Ports ---
    sc_in_clk                    clock;
    sc_in<bool>                  rst;
    sc_in<bool>                  in_valid;
    sc_vector<sc_in<double>>     img{"img", 150528};

    sc_out<FeatureMapSignal>     out_data;
    sc_out<bool>                 out_valid;

    // --- Internal computation ---
    InputLayerImpl   input_layer;
    ConvLayerImpl    conv1;
    MaxPoolLayerImpl pool1;

    void load_weights(const std::string& dir) {
        // Conv1: 64 kernels 11x11, stride 4, no padding (padded in InputLayer)
        conv1.in_channels  = 3;
        conv1.out_channels = 64;
        conv1.kernel_size  = 11;
        conv1.stride       = 4;
        conv1.padding      = 0;
        conv1.in_h         = 227;
        conv1.in_w         = 227;
        conv1.load_weights(dir + "/conv1_weight.txt", dir + "/conv1_bias.txt");
        // output: (227-11)/4+1 = 55 => [64][55][55]

        // Pool1: 3x3, stride 2
        pool1.channels   = 64;
        pool1.pool_size  = 3;
        pool1.stride     = 2;
        pool1.in_h       = 55;
        pool1.in_w       = 55;
        // output: [64][27][27]
    }

    void run() {
        if (rst.read()) {
            out_valid.write(false);
            return;
        }

        if (in_valid.read()) {
            // Read image pixels from Pattern
            std::vector<double> raw(150528);
            for (int i = 0; i < 150528; ++i)
                raw[i] = img[i].read();

            // Forward
            input_layer.load_data_and_pad(raw);
            conv1.process(input_layer.output);
            pool1.process(conv1.output);

            // Write to channel
            FeatureMapSignal s;
            s.data = pool1.output;
            out_data.write(s);
            out_valid.write(true);
        } else {
            out_valid.write(false);
        }
    }

    SC_CTOR(ModuleA) {
        SC_METHOD(run);
        sensitive << clock.pos();
    }
};

#endif // MODULE_A_H