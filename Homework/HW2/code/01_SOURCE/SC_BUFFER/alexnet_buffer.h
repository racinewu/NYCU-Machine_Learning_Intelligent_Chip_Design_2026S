#ifndef ALEXNET_BUFFER_H
#define ALEXNET_BUFFER_H

#include "layers.h"
#include <systemc.h>
#include "moduleA.h"
#include "moduleB.h"
#include "moduleC.h"
#include <string>

// ============================================================
// AlexNet Top Module - sc_buffer version
//
//   Connects Pattern <-> ModuleA -[sc_buffer]-> ModuleB -[sc_buffer]-> ModuleC
// ============================================================
SC_MODULE(AlexNet) {
    // --- Ports (connect with Pattern) ---
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

    // --- Internal sc_buffer channels ---
    sc_buffer<FeatureMapSignal> sig_a_to_b;   // [64][27][27]
    sc_buffer<bool>             sig_valid_ab;
    sc_buffer<FlatSignal>       sig_b_to_c;   // [9216]
    sc_buffer<bool>             sig_valid_bc;

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
        module_a->clock(clock);
        module_a->rst(rst);
        module_a->in_valid(in_valid);
        for (int i = 0; i < 150528; ++i)
            module_a->img[i](img[i]);
        module_a->out_data(sig_a_to_b);
        module_a->out_valid(sig_valid_ab);

        // --- Connect ModuleB ---
        module_b->clock(clock);
        module_b->rst(rst);
        module_b->in_data(sig_a_to_b);
        module_b->in_valid(sig_valid_ab);
        module_b->out_data(sig_b_to_c);
        module_b->out_valid(sig_valid_bc);

        // --- Connect ModuleC ---
        module_c->clock(clock);
        module_c->rst(rst);
        module_c->in_data(sig_b_to_c);
        module_c->in_valid(sig_valid_bc);
        module_c->out_valid(out_valid);
        for (int i = 0; i < 1000; ++i) {
            module_c->output_softmax[i](output_softmax[i]);
            module_c->output_linear[i](output_linear[i]);
        }
    }

    ~AlexNet() {
        delete module_a;
        delete module_b;
        delete module_c;
    }
};

#endif // ALEXNET_BUFFER_H