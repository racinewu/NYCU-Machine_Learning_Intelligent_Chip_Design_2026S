#include "alexnet.h"
#include <iostream>

using namespace std;

AlexNet::AlexNet(sc_module_name name) : sc_module(name) {
    input_layer = new InputLayer("input_layer");
    conv1       = new ConvLayer("conv1");
    pool1       = new MaxPoolLayer("pool1");
    conv2       = new ConvLayer("conv2");
    pool2       = new MaxPoolLayer("pool2");
    conv3       = new ConvLayer("conv3");
    conv4       = new ConvLayer("conv4");
    conv5       = new ConvLayer("conv5");
    pool5       = new MaxPoolLayer("pool5");
    fc6         = new FCLayer("fc6");
    fc7         = new FCLayer("fc7");
    fc8         = new FCLayer("fc8");
    softmax     = new SoftmaxLayer("softmax");
}

AlexNet::~AlexNet() {
    delete input_layer;
    delete conv1; delete pool1;
    delete conv2; delete pool2;
    delete conv3; delete conv4; delete conv5; delete pool5;
    delete fc6; delete fc7; delete fc8;
    delete softmax;
}

// Build: configure layer parameters and load weights
void AlexNet::build(const string& weight_dir) {
    data_dir = weight_dir;
    init_layers();
    load_weights();
}

void AlexNet::init_layers() {
    // --- Conv1: 64 kernels 11x11, stride 4, no extra padding (already padded in input) ---
    conv1->in_channels  = 3;
    conv1->out_channels = 64;
    conv1->kernel_size  = 11;
    conv1->stride       = 4;
    conv1->padding      = 0;  // padding was done in InputLayer
    conv1->in_h         = 227;
    conv1->in_w         = 227;
    // output: (227-11)/4+1 = 55 => 55x55x64

    // --- Pool1: 3x3, stride 2 ---
    pool1->channels   = 64;
    pool1->pool_size  = 3;
    pool1->stride     = 2;
    pool1->in_h       = 55;
    pool1->in_w       = 55;
    // output: (55-3)/2+1 = 27 => 27x27x64

    // --- Conv2: 192 kernels 5x5, stride 1, padding 2 ---
    conv2->in_channels  = 64;
    conv2->out_channels = 192;
    conv2->kernel_size  = 5;
    conv2->stride       = 1;
    conv2->padding      = 2;
    conv2->in_h         = 27;
    conv2->in_w         = 27;
    // output: (27+4-5)/1+1 = 27 => 27x27x192

    // --- Pool2: 3x3, stride 2 ---
    pool2->channels   = 192;
    pool2->pool_size  = 3;
    pool2->stride     = 2;
    pool2->in_h       = 27;
    pool2->in_w       = 27;
    // output: 13x13x192

    // --- Conv3: 384 kernels 3x3, stride 1, padding 1 ---
    conv3->in_channels  = 192;
    conv3->out_channels = 384;
    conv3->kernel_size  = 3;
    conv3->stride       = 1;
    conv3->padding      = 1;
    conv3->in_h         = 13;
    conv3->in_w         = 13;
    // output: 13x13x384

    // --- Conv4: 256 kernels 3x3, stride 1, padding 1 ---
    conv4->in_channels  = 384;
    conv4->out_channels = 256;
    conv4->kernel_size  = 3;
    conv4->stride       = 1;
    conv4->padding      = 1;
    conv4->in_h         = 13;
    conv4->in_w         = 13;
    // output: 13x13x256

    // --- Conv5: 256 kernels 3x3, stride 1, padding 1 ---
    conv5->in_channels  = 256;
    conv5->out_channels = 256;
    conv5->kernel_size  = 3;
    conv5->stride       = 1;
    conv5->padding      = 1;
    conv5->in_h         = 13;
    conv5->in_w         = 13;
    // output: 13x13x256

    // --- Pool5: 3x3, stride 2 ---
    pool5->channels   = 256;
    pool5->pool_size  = 3;
    pool5->stride     = 2;
    pool5->in_h       = 13;
    pool5->in_w       = 13;
    // output: 6x6x256 => flatten = 9216

    // --- FC6: 9216 -> 4096 + ReLU ---
    fc6->in_features  = 9216;
    fc6->out_features = 4096;
    fc6->use_relu     = true;

    // --- FC7: 4096 -> 4096 + ReLU ---
    fc7->in_features  = 4096;
    fc7->out_features = 4096;
    fc7->use_relu     = true;

    // --- FC8: 4096 -> 1000, no ReLU ---
    fc8->in_features  = 4096;
    fc8->out_features = 1000;
    fc8->use_relu     = false;

    // --- Softmax: 1000 ---
    softmax->size = 1000;
}

void AlexNet::load_weights() {
    cout << "[INFO] Loading weights from: " << data_dir << endl;
    conv1->load_weights(data_dir + "/conv1_weight.txt", data_dir + "/conv1_bias.txt");
    conv2->load_weights(data_dir + "/conv2_weight.txt", data_dir + "/conv2_bias.txt");
    conv3->load_weights(data_dir + "/conv3_weight.txt", data_dir + "/conv3_bias.txt");
    conv4->load_weights(data_dir + "/conv4_weight.txt", data_dir + "/conv4_bias.txt");
    conv5->load_weights(data_dir + "/conv5_weight.txt", data_dir + "/conv5_bias.txt");
    fc6->load_weights(data_dir + "/fc6_weight.txt", data_dir + "/fc6_bias.txt");
    fc7->load_weights(data_dir + "/fc7_weight.txt", data_dir + "/fc7_bias.txt");
    fc8->load_weights(data_dir + "/fc8_weight.txt", data_dir + "/fc8_bias.txt");
    cout << "[INFO] Weights loaded." << endl;
}

// Forward Pass
void AlexNet::forward(const string& image_path) {
    cout << "[INFO] Loading image: " << image_path << endl;

    // Layer 0: Input + zero padding
    input_layer->load_data_and_pad(image_path);

    // Layer 1: Conv1
    conv1->input = &input_layer->output;
    conv1->process();
    cout << "[INFO] Conv1 done." << endl;

    // Layer 1b: Pool1
    pool1->input = &conv1->output;
    pool1->process();
    cout << "[INFO] Pool1 done." << endl;

    // Layer 2: Conv2
    conv2->input = &pool1->output;
    conv2->process();
    cout << "[INFO] Conv2 done." << endl;

    // Layer 2b: Pool2
    pool2->input = &conv2->output;
    pool2->process();
    cout << "[INFO] Pool2 done." << endl;

    // Layer 3: Conv3
    conv3->input = &pool2->output;
    conv3->process();
    cout << "[INFO] Conv3 done." << endl;

    // Layer 4: Conv4
    conv4->input = &conv3->output;
    conv4->process();
    cout << "[INFO] Conv4 done." << endl;

    // Layer 5: Conv5
    conv5->input = &conv4->output;
    conv5->process();
    cout << "[INFO] Conv5 done." << endl;

    // Layer 5b: Pool5
    pool5->input = &conv5->output;
    pool5->process();
    cout << "[INFO] Pool5 done." << endl;

    // Flatten: 256 x 6 x 6 = 9216
    flatten_pool5();

    // Layer 6: FC6
    fc6->input = &flat;
    fc6->process();
    cout << "[INFO] FC6 done." << endl;

    // Layer 7: FC7
    fc7->input = &fc6->output;
    fc7->process();
    cout << "[INFO] FC7 done." << endl;

    // Layer 8: FC8
    fc8->input = &fc7->output;
    fc8->process();
    cout << "[INFO] FC8 done." << endl;

    // Layer 9: Softmax
    softmax->input = &fc8->output;
    softmax->process();
    probabilities = softmax->output;
    cout << "[INFO] Softmax done." << endl;
}

// Flatten pool5 output: [256][6][6] -> flat[9216]
// Channel-first raster scan
void AlexNet::flatten_pool5() {
    flat.clear();
    flat.reserve(9216);
    for (int c = 0; c < 256; ++c)
        for (int r = 0; r < 6; ++r)
            for (int col = 0; col < 6; ++col)
                flat.push_back(pool5->output[c][r][col]);
}