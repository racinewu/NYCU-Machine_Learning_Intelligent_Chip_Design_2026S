#include <systemc.h>
#include "alexnet.h"
#include "monitor.h"
#include <iostream>
#include <string>

using namespace std;

int sc_main(int argc, char* argv[]) {
    sc_clock clk("clk", 1, SC_NS);
    sc_signal<bool> reset;

    if (argc != 2) {
        cerr << "Usage: ./run <cat|dog>" << endl;
        return 1;
    }

    string file = argv[1];

    AlexNet* top     = new AlexNet("AlexNet");
    Monitor* monitor = new Monitor("Monitor");

    // -------------------------------------------------------
    // Configure paths
    //   ./data/dog.txt  or  ./data/cat.txt
    //   ./data/conv1_weight.txt  etc.
    //   ./data/imagenet_classes.txt
    // -------------------------------------------------------
    string data_dir    = "./data";
    string image_path = data_dir + "/" + file;
    string classes_path = data_dir + "/imagenet_classes.txt";

    top->build(data_dir);

    top->forward(image_path);

    monitor->print_top100_with_val(top->fc8->output, top->probabilities, classes_path);

    delete top;
    delete monitor;

    return 0;
}