#ifndef MODULE_B_H
#define MODULE_B_H

#include "layers.h"
#include <systemc.h>

// ============================================================
// Module B (FIFO version):
//   Conv2+ReLU+Pool2 -> Conv3+ReLU -> Conv4+ReLU -> Conv5+ReLU -> Pool5
//
//   Ports in  : clock, rst, sc_fifo_in<FeatureMapSignal>
//   Ports out : sc_fifo_out<FlatSignal>
//
//   SC_THREAD + blocking fifo.read() / fifo.write()
//   Doesn't need in_valid / out_valid
// ============================================================
SC_MODULE(ModuleB) {
    // --- Ports ---
    sc_in_clk                    clock;
    sc_in<bool>                  rst;

    sc_fifo_in<FeatureMapSignal>  in_data;
    sc_fifo_out<FlatSignal>       out_data;

    // --- Internal computation ---
    ConvLayerImpl    conv2;
    MaxPoolLayerImpl pool2;
    ConvLayerImpl    conv3;
    ConvLayerImpl    conv4;
    ConvLayerImpl    conv5;
    MaxPoolLayerImpl pool5;

    void load_weights(const std::string& dir) {
        // Conv2: 192 kernels 5x5, stride 1, padding 2
        conv2.in_channels  = 64;
        conv2.out_channels = 192;
        conv2.kernel_size  = 5;
        conv2.stride       = 1;
        conv2.padding      = 2;
        conv2.in_h         = 27;
        conv2.in_w         = 27;
        conv2.load_weights(dir + "/conv2_weight.txt", dir + "/conv2_bias.txt");

        // Pool2: 3x3, stride 2
        pool2.channels  = 192;
        pool2.pool_size = 3;
        pool2.stride    = 2;
        pool2.in_h      = 27;
        pool2.in_w      = 27;

        // Conv3: 384 kernels 3x3, stride 1, padding 1
        conv3.in_channels  = 192;
        conv3.out_channels = 384;
        conv3.kernel_size  = 3;
        conv3.stride       = 1;
        conv3.padding      = 1;
        conv3.in_h         = 13;
        conv3.in_w         = 13;
        conv3.load_weights(dir + "/conv3_weight.txt", dir + "/conv3_bias.txt");

        // Conv4: 256 kernels 3x3, stride 1, padding 1
        conv4.in_channels  = 384;
        conv4.out_channels = 256;
        conv4.kernel_size  = 3;
        conv4.stride       = 1;
        conv4.padding      = 1;
        conv4.in_h         = 13;
        conv4.in_w         = 13;
        conv4.load_weights(dir + "/conv4_weight.txt", dir + "/conv4_bias.txt");

        // Conv5: 256 kernels 3x3, stride 1, padding 1
        conv5.in_channels  = 256;
        conv5.out_channels = 256;
        conv5.kernel_size  = 3;
        conv5.stride       = 1;
        conv5.padding      = 1;
        conv5.in_h         = 13;
        conv5.in_w         = 13;
        conv5.load_weights(dir + "/conv5_weight.txt", dir + "/conv5_bias.txt");

        // Pool5: 3x3, stride 2
        pool5.channels  = 256;
        pool5.pool_size = 3;
        pool5.stride    = 2;
        pool5.in_h      = 13;
        pool5.in_w      = 13;
        // output: [256][6][6]
    }

    void run() {
        while (true) {
            // Blocking read: waits for ModuleA to produce data
            FeatureMapSignal input_sig = in_data.read();

            if (rst.read()) continue;

            const FeatureMap& input = input_sig.data;

            conv2.process(input);
            pool2.process(conv2.output);
            conv3.process(pool2.output);
            conv4.process(conv3.output);
            conv5.process(conv4.output);
            pool5.process(conv5.output);

            // Flatten [256][6][6] -> [9216], channel-first
            FlatSignal s;
            s.data.reserve(9216);
            for (int c = 0; c < 256; ++c)
                for (int r = 0; r < 6; ++r)
                    for (int col = 0; col < 6; ++col)
                        s.data.push_back(pool5.output[c][r][col]);

            // Blocking write: waits for ModuleC to have available space
            out_data.write(s);
        }
    }

    SC_CTOR(ModuleB) {
        SC_THREAD(run);
        // Driven by the data_written event of sc_fifo_in
    }
};

#endif // MODULE_B_H