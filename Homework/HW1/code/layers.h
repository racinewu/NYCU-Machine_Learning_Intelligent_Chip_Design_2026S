#ifndef LAYERS_H
#define LAYERS_H

#include <systemc.h>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <iostream>

// ============================================================
// Utility: load a flat txt file into a float vector
// ============================================================
inline std::vector<float> load_txt(const std::string& path) {
    std::vector<float> data;
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "[ERROR] Cannot open: " << path << std::endl;
        return data;
    }
    float v;
    while (f >> v) data.push_back(v);
    return data;
}

// ============================================================
// InputLayer: zero-pad 224x224x3 -> 227x227x3
//   top/left: 2 rows/cols of zeros
//   bottom/right: 1 row/col of zeros
// ============================================================
SC_MODULE(InputLayer) {
    // Output: padded image [channel][row][col]
    std::vector<std::vector<std::vector<float>>> output; // [3][227][227]

    SC_CTOR(InputLayer) : output(3, std::vector<std::vector<float>>(227, std::vector<float>(227, 0.0f))) {}

    // Read raw image txt (raster scan: row-major, channel last dimension = [row][col][ch])
    // The txt stores pixels in order: R[0][0], G[0][0], B[0][0], R[0][1], ...
    // Actually per HW: image reading format is channel-first raster scan
    // Looking at the diagram: [3] channels, [2] rows, [1] cols => channel-first
    void load_data_and_pad(const std::string& path) {
        std::vector<float> raw = load_txt(path);
        // raw size should be 224*224*3 = 150528
        // Format: channel-first raster scan => raw[c * 224*224 + r*224 + col]
        for (int c = 0; c < 3; ++c)
            for (int r = 0; r < 224; ++r)
                for (int col = 0; col < 224; ++col)
                    output[c][r + 2][col + 2] = raw[c * 224 * 224 + r * 224 + col];
    }
};

// ============================================================
// ConvLayer: Convolution + ReLU
// ============================================================
SC_MODULE(ConvLayer) {
    // Parameters (set before process())
    int in_channels, out_channels;
    int kernel_size, stride, padding;
    int in_h, in_w;

    std::vector<float> weights; // [out_ch][in_ch][kH][kW] flattened
    std::vector<float> biases;  // [out_ch]

    // Input / Output feature maps [ch][row][col]
    std::vector<std::vector<std::vector<float>>>* input;
    std::vector<std::vector<std::vector<float>>>  output;

    SC_CTOR(ConvLayer) : input(nullptr) {}

    void load_weights(const std::string& wpath, const std::string& bpath) {
        weights = load_txt(wpath);
        biases  = load_txt(bpath);
    }

    void process() {
        int out_h = (in_h + 2 * padding - kernel_size) / stride + 1;
        int out_w = (in_w + 2 * padding - kernel_size) / stride + 1;

        // Pad input if needed
        int pad_h = in_h + 2 * padding;
        int pad_w = in_w + 2 * padding;
        std::vector<std::vector<std::vector<float>>> padded(
            in_channels, std::vector<std::vector<float>>(pad_h, std::vector<float>(pad_w, 0.0f)));
        for (int c = 0; c < in_channels; ++c)
            for (int r = 0; r < in_h; ++r)
                for (int col = 0; col < in_w; ++col)
                    padded[c][r + padding][col + padding] = (*input)[c][r][col];

        output.assign(out_channels, std::vector<std::vector<float>>(out_h, std::vector<float>(out_w, 0.0f)));

        int k2 = kernel_size * kernel_size;
        for (int oc = 0; oc < out_channels; ++oc) {
            for (int oh = 0; oh < out_h; ++oh) {
                for (int ow = 0; ow < out_w; ++ow) {
                    float sum = biases[oc];
                    for (int ic = 0; ic < in_channels; ++ic) {
                        int w_base = ((oc * in_channels) + ic) * k2;
                        for (int kh = 0; kh < kernel_size; ++kh) {
                            for (int kw = 0; kw < kernel_size; ++kw) {
                                sum += padded[ic][oh * stride + kh][ow * stride + kw]
                                     * weights[w_base + kh * kernel_size + kw];
                            }
                        }
                    }
                    // ReLU
                    output[oc][oh][ow] = (sum > 0.0f) ? sum : 0.0f;
                }
            }
        }
    }
};

// ============================================================
// MaxPoolLayer: Max Pooling
// ============================================================
SC_MODULE(MaxPoolLayer) {
    int channels, pool_size, stride;
    int in_h, in_w;

    std::vector<std::vector<std::vector<float>>>* input;
    std::vector<std::vector<std::vector<float>>>  output;

    SC_CTOR(MaxPoolLayer) : input(nullptr) {}

    void process() {
        int out_h = (in_h - pool_size) / stride + 1;
        int out_w = (in_w - pool_size) / stride + 1;
        output.assign(channels, std::vector<std::vector<float>>(out_h, std::vector<float>(out_w, -1e30f)));

        for (int c = 0; c < channels; ++c)
            for (int oh = 0; oh < out_h; ++oh)
                for (int ow = 0; ow < out_w; ++ow) {
                    float mx = -1e30f;
                    for (int kh = 0; kh < pool_size; ++kh)
                        for (int kw = 0; kw < pool_size; ++kw)
                            mx = std::max(mx, (*input)[c][oh * stride + kh][ow * stride + kw]);
                    output[c][oh][ow] = mx;
                }
    }
};

// ============================================================
// FCLayer: Fully Connected + optional ReLU
// ============================================================
SC_MODULE(FCLayer) {
    int in_features, out_features;
    bool use_relu;

    std::vector<float> weights; // [out][in]
    std::vector<float> biases;  // [out]

    std::vector<float>* input;
    std::vector<float>  output;

    SC_CTOR(FCLayer) : input(nullptr), use_relu(true) {}

    void load_weights(const std::string& wpath, const std::string& bpath) {
        weights = load_txt(wpath);
        biases  = load_txt(bpath);
    }

    void process() {
        output.assign(out_features, 0.0f);
        for (int o = 0; o < out_features; ++o) {
            float sum = biases[o];
            for (int i = 0; i < in_features; ++i)
                sum += (*input)[i] * weights[o * in_features + i];
            output[o] = (use_relu && sum < 0.0f) ? 0.0f : sum;
        }
    }
};

// ============================================================
// SoftmaxLayer
// ============================================================
SC_MODULE(SoftmaxLayer) {
    std::vector<float>* input;
    std::vector<float>  output;
    int size;

    SC_CTOR(SoftmaxLayer) : input(nullptr) {}

    void process() {
        output.resize(size);
        float mx = *std::max_element(input->begin(), input->end());
        float sum = 0.0f;
        for (int i = 0; i < size; ++i) {
            output[i] = std::exp((*input)[i] - mx);
            sum += output[i];
        }
        for (int i = 0; i < size; ++i) output[i] /= sum;
    }
};

#endif // LAYERS_H