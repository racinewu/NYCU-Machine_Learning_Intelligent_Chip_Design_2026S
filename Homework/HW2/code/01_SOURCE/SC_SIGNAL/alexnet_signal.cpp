#include "layers.h"          // must come first (defines wrappers before systemc.h)
#include "alexnet_signal.h"
#include "Pattern.h"
#include <iostream>
#include <string>

using namespace std;

int sc_main(int argc, char* argv[]) {
    if (argc != 2) {
        cerr << "Usage: ./run <dog|cat>" << endl;
        return 1;
    }

    string img_name = string(argv[1]);  // e.g. "dog.txt"

    // ---- Clock & shared signals ----
    sc_clock clk("clk", 1, SC_NS);

    sc_signal<bool>               sig_rst;
    sc_signal<bool>               sig_in_valid;
    sc_signal<bool>               sig_out_valid;
    sc_vector<sc_signal<double>>  sig_img        ("sig_img",         150528);
    sc_vector<sc_signal<double>>  sig_out_softmax("sig_out_softmax", 1000);
    sc_vector<sc_signal<double>>  sig_out_linear ("sig_out_linear",  1000);

    // ---- Pattern ----
    Pattern pattern("Pattern", img_name);
    pattern.clock    (clk);
    pattern.rst      (sig_rst);
    pattern.in_valid (sig_in_valid);
    pattern.out_valid(sig_out_valid);
    for (int i = 0; i < 150528; ++i)
        pattern.img[i](sig_img[i]);
    for (int i = 0; i < 1000; ++i) {
        pattern.output_softmax[i](sig_out_softmax[i]);
        pattern.output_linear[i] (sig_out_linear[i]);
    }

    // ---- AlexNet Top ----
    AlexNet alexnet("AlexNet");
    alexnet.clock    (clk);
    alexnet.rst      (sig_rst);
    alexnet.in_valid (sig_in_valid);
    alexnet.out_valid(sig_out_valid);
    for (int i = 0; i < 150528; ++i)
        alexnet.img[i](sig_img[i]);
    for (int i = 0; i < 1000; ++i) {
        alexnet.output_softmax[i](sig_out_softmax[i]);
        alexnet.output_linear[i] (sig_out_linear[i]);
    }

    // ---- Load weights ----
    alexnet.load_weights("../../00_TESTBED/data");

    // ---- Run ----
    sc_start();
    return 0;
}