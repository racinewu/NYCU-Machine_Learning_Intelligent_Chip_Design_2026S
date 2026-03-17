#include <systemc.h>
#include "alexnet.h"
#include "monitor.h"
#include <iostream>
#include <string>

using namespace std;

// ============================================================
// sc_main
// Usage: ./alexnet <dog|cat>
//        make dog  =>  ./alexnet dog
//        make cat  =>  ./alexnet cat
// ============================================================
int sc_main(int argc, char* argv[]) {
    sc_clock clk("clk", 1, SC_NS);
    sc_signal<bool> reset;

    // Ensure exactly one filename argument is provided
    if (argc != 2) {
        cerr << "Usage: " << argv[0] << " <file>" << endl;
        return 1;
    }

    // Get the filename (dog or cat) from argument
    string file = argv[1];

    // -------------------------------------------------------
    // Instantiate modules
    // -------------------------------------------------------
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

    // -------------------------------------------------------
    // Build network (load all weights)
    // -------------------------------------------------------
    top->build(data_dir);

    // -------------------------------------------------------
    // Forward pass
    // -------------------------------------------------------
    top->forward(image_path);

    // -------------------------------------------------------
    // Monitor: print top-100 results
    // fc8->output = raw logits, top->probabilities = softmax output
    // -------------------------------------------------------
    monitor->print_top100_with_val(top->fc8->output, top->probabilities, classes_path);

    // -------------------------------------------------------
    // Cleanup
    // -------------------------------------------------------
    delete top;
    delete monitor;

    return 0;
}