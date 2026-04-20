#ifndef ALEXNET_FIFO_H
#define ALEXNET_FIFO_H

#include "layers.h"
#include <systemc.h>
#include "moduleA.h"
#include "moduleB.h"
#include "moduleC.h"
#include <string>

// ============================================================
// AlexNet Top Module - sc_fifo version
//
//   Pattern <-> ModuleA -[sc_fifo]-> ModuleB -[sc_fifo]-> ModuleC
//
//   Different from signal/buffer ver.: 
//   1. Internal channel change to  sc_fifo
//   2. ModuleB/C doesn't need in_valid / out_valid connection
// ============================================================
SC_MODULE(AlexNet) {
    // --- Ports (connect with Pattern, same as signal/buffer ver.) ---
    sc_in_clk                   clock;
    sc_in<bool>                 rst;
    sc_in<bool>                 in_valid;
    sc_vector<sc_in<double>>    img{"img", 150528};

    sc_out<bool>                out_valid;
    sc_vector<sc_out<double>>   output_softmax{"output_softmax", 1000};
    sc_vector<sc_out<double>>   output_linear {"output_linear",  1000};

    // --- Sub-modules ---
    ModuleA* module_a;
    ModuleB* module_b;
    ModuleC* module_c;

    // --- Internal sc_fifo channels (size=1) ---
    sc_fifo<FeatureMapSignal>   fifo_a_to_b{1};  // ModuleA -> ModuleB
    sc_fifo<FlatSignal>         fifo_b_to_c{1};  // ModuleB -> ModuleC

    void load_weights(const std::string& dir) {
        module_a->load_weights(dir);
        module_b->load_weights(dir);
        module_c->load_weights(dir);
    }

    SC_CTOR(AlexNet) {
        module_a = new ModuleA("ModuleA");
        module_b = new ModuleB("ModuleB");
        module_c = new ModuleC("ModuleC");

        // --- Connect ModuleA ---
        module_a->clock   (clock);
        module_a->rst     (rst);
        module_a->in_valid(in_valid);
        for (int i = 0; i < 150528; ++i)
            module_a->img[i](img[i]);
        module_a->out_data(fifo_a_to_b);

        // --- Connect ModuleB ---
        module_b->clock  (clock);
        module_b->rst    (rst);
        module_b->in_data(fifo_a_to_b);
        module_b->out_data(fifo_b_to_c);

        // --- Connect ModuleC ---
        module_c->clock        (clock);
        module_c->rst          (rst);
        module_c->in_data      (fifo_b_to_c);
        module_c->out_valid    (out_valid);
        for (int i = 0; i < 1000; ++i) {
            module_c->output_softmax[i](output_softmax[i]);
            module_c->output_linear[i] (output_linear[i]);
        }
    }

    ~AlexNet() {
        delete module_a;
        delete module_b;
        delete module_c;
    }
};

#endif // ALEXNET_FIFO_H