#ifndef LAYERS_H
#define LAYERS_H

#include <vector>
#include <string>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <iostream>

using FeatureMap = std::vector<std::vector<std::vector<float>>>;

inline std::vector<float> load_txt(const std::string& path) {
    std::vector<float> r;
    std::ifstream f(path);
    if (!f.is_open()) { std::cerr << "[ERROR] Cannot open: " << path << std::endl; return r; }
    float v; while (f >> v) r.push_back(v);
    return r;
}

// Load a contiguous slice of output-channel rows from a conv weight file.
// Weight layout on disk: [Cout][Cin][K][K] row-major.
inline void load_partial_weights(const std::string& wpath, const std::string& bpath,
                                  int Cin, int Cout_total, int K,
                                  int oc_start, int oc_count,
                                  std::vector<float>& weights,
                                  std::vector<float>& biases) {
    int kf = Cin * K * K;
    std::ifstream fw(wpath);
    if (!fw.is_open()) { std::cerr << "[ERROR] " << wpath << std::endl; return; }
    float tmp;
    for (int i = 0; i < oc_start * kf; i++) fw >> tmp;
    weights.resize(oc_count * kf);
    for (int i = 0; i < oc_count * kf; i++) fw >> weights[i];

    std::ifstream fb(bpath);
    if (!fb.is_open()) { std::cerr << "[ERROR] " << bpath << std::endl; return; }
    for (int i = 0; i < oc_start; i++) fb >> tmp;
    biases.resize(oc_count);
    for (int i = 0; i < oc_count; i++) fb >> biases[i];
}

// Load a contiguous slice of neuron rows from an FC weight file.
// Weight layout: [Nout][Nin] row-major.
inline void load_partial_fc(const std::string& wpath, const std::string& bpath,
                             int Nin, int Nout_total,
                             int n_start, int n_count,
                             std::vector<float>& weights,
                             std::vector<float>& biases) {
    std::ifstream fw(wpath);
    if (!fw.is_open()) { std::cerr << "[ERROR] " << wpath << std::endl; return; }
    float tmp;
    for (int i = 0; i < n_start * Nin; i++) fw >> tmp;
    weights.resize(n_count * Nin);
    for (int i = 0; i < n_count * Nin; i++) fw >> weights[i];

    std::ifstream fb(bpath);
    if (!fb.is_open()) { std::cerr << "[ERROR] " << bpath << std::endl; return; }
    for (int i = 0; i < n_start; i++) fb >> tmp;
    biases.resize(n_count);
    for (int i = 0; i < n_count; i++) fb >> biases[i];
}

struct InputLayerImpl {
    FeatureMap output;
    InputLayerImpl()
        : output(3, std::vector<std::vector<float>>(227, std::vector<float>(227, 0.f))) {}
    void load_data_and_pad(const std::vector<float>& raw) {
        for (auto& ch : output) for (auto& row : ch) std::fill(row.begin(), row.end(), 0.f);
        for (int c=0; c<3; c++)
            for (int r=0; r<224; r++)
                for (int col=0; col<224; col++)
                    output[c][r+2][col+2] = raw[c*224*224+r*224+col];
    }
};

struct ConvLayerImpl {
    int Cin=0, Cout=0, K=0, stride=1, pad=0, Hin=0, Win=0;
    std::vector<float> weights, biases;
    FeatureMap output;

    void load_weights_partial(const std::string& wp, const std::string& bp,
                               int cin_total, int cout_total, int k,
                               int oc_start, int oc_count) {
        Cin=cin_total; Cout=oc_count; K=k;
        load_partial_weights(wp, bp, cin_total, cout_total, k, oc_start, oc_count, weights, biases);
    }

    void process(const FeatureMap& in) {
        int oh = (Hin + 2*pad - K)/stride + 1;
        int ow = (Win + 2*pad - K)/stride + 1;
        FeatureMap padded(Cin, std::vector<std::vector<float>>(Hin+2*pad, std::vector<float>(Win+2*pad,0.f)));
        for (int c=0;c<Cin;c++)
            for (int r=0;r<Hin;r++)
                for (int col=0;col<Win;col++)
                    padded[c][r+pad][col+pad] = in[c][r][col];
        output.assign(Cout, std::vector<std::vector<float>>(oh, std::vector<float>(ow,0.f)));
        int k2 = K*K;
        for (int oc=0;oc<Cout;oc++)
            for (int i=0;i<oh;i++)
                for (int j=0;j<ow;j++) {
                    float s = biases[oc];
                    for (int ic=0;ic<Cin;ic++) {
                        int wb = (oc*Cin+ic)*k2;
                        for (int kh=0;kh<K;kh++)
                            for (int kw=0;kw<K;kw++)
                                s += padded[ic][i*stride+kh][j*stride+kw]*weights[wb+kh*K+kw];
                    }
                    output[oc][i][j] = (s>0.f)?s:0.f;
                }
    }
};

struct MaxPoolLayerImpl {
    int channels=0, ps=0, stride=1, Hin=0, Win=0;
    FeatureMap output;
    void process(const FeatureMap& in) {
        int oh=(Hin-ps)/stride+1, ow=(Win-ps)/stride+1;
        output.assign(channels, std::vector<std::vector<float>>(oh, std::vector<float>(ow,-1e30f)));
        for (int c=0;c<channels;c++)
            for (int i=0;i<oh;i++)
                for (int j=0;j<ow;j++) {
                    float mx=-1e30f;
                    for (int kh=0;kh<ps;kh++)
                        for (int kw=0;kw<ps;kw++)
                            mx=std::max(mx,in[c][i*stride+kh][j*stride+kw]);
                    output[c][i][j]=mx;
                }
    }
};

struct FCLayerImpl {
    int Nin=0, Nout=0;
    bool relu=true;
    std::vector<float> weights, biases, output;

    void load_weights_partial(const std::string& wp, const std::string& bp,
                               int nin, int nout_total, int n_start, int n_count) {
        Nin=nin; Nout=n_count;
        load_partial_fc(wp, bp, nin, nout_total, n_start, n_count, weights, biases);
    }

    void process(const std::vector<float>& in) {
        output.assign(Nout, 0.f);
        for (int o=0;o<Nout;o++) {
            float s=biases[o];
            for (int i=0;i<Nin;i++) s+=in[i]*weights[o*Nin+i];
            output[o] = (relu && s<0.f) ? 0.f : s;
        }
    }
};

struct SoftmaxImpl {
    std::vector<float> output;
    void process(const std::vector<float>& in) {
        int n=in.size();
        output.resize(n);
        float mx=*std::max_element(in.begin(),in.end());
        float sum=0.f;
        for (int i=0;i<n;i++) { output[i]=std::exp(in[i]-mx); sum+=output[i]; }
        for (int i=0;i<n;i++) output[i]/=sum;
    }
};

#endif