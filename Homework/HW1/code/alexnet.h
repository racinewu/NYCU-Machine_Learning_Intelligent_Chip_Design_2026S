#ifndef ALEXNET_H
#define ALEXNET_H

#include <systemc.h>
#include "layers.h"
#include <string>
#include <vector>

// ============================================================
// AlexNet Top-Level Module
// Connects: Input -> Conv1 -> Pool1 -> Conv2 -> Pool2 ->
//           Conv3 -> Conv4 -> Conv5 -> Pool5 ->
//           FC6 -> FC7 -> FC8 -> Softmax
// ============================================================
SC_MODULE(AlexNet) {

    // --- Layer instances ---
    InputLayer*   input_layer;
    ConvLayer*    conv1;
    MaxPoolLayer* pool1;
    ConvLayer*    conv2;
    MaxPoolLayer* pool2;
    ConvLayer*    conv3;
    ConvLayer*    conv4;
    ConvLayer*    conv5;
    MaxPoolLayer* pool5;
    FCLayer*      fc6;
    FCLayer*      fc7;
    FCLayer*      fc8;
    SoftmaxLayer* softmax;

    // Flattened vector connecting pool5 -> fc6
    std::vector<float> flat;

    // Final probabilities (1000)
    std::vector<float> probabilities;

    // Path to weight files
    std::string data_dir;

    SC_CTOR(AlexNet);
    ~AlexNet();

    void build(const std::string& weight_dir);
    void forward(const std::string& image_path);

private:
    void init_layers();
    void load_weights();
    void flatten_pool5();
};

#endif // ALEXNET_H