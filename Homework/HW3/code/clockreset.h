#ifndef CLOCKRESET_H
#define CLOCKRESET_H

#include <systemc.h>

SC_MODULE( Clock )
{
public:
    sc_out <bool> clk;
    sc_clock clk_intern;
    int count;
    void run();

    SC_HAS_PROCESS( Clock );

    // 5-stage pipeline critical path = Arbiter + XB = 0.23 + 0.23 = 0.46 ns
    // Clock period = 0.5 ns > 0.46 ns  (satisfies timing constraint)
    // Stage delays:
    //   Stage 1: Sync In      = 0.45 ns
    //   Stage 2: EB           = 0.44 ns
    //   Stage 3: RC           = 0.30 ns
    //   Stage 4: Arbiter + XB = 0.46 ns
    //   Stage 5: Sync Out     = 0.45 ns
    Clock( sc_module_name name, int cycle_time):
           sc_module( name ),
           clk( "clk" ),
           clk_intern( sc_gen_unique_name( name ), 0.5, SC_NS )
    {
        SC_METHOD( run );
        count = 0;
        sensitive << clk_intern;
    }
};

SC_MODULE( Reset )
{
    sc_out<bool> rst_n;
    int wait_time;
    void run();

    SC_HAS_PROCESS( Reset );

    Reset( sc_module_name name, int reset_time):
           sc_module( name ),
           rst_n( "rst_n" ),
           wait_time( reset_time )
    {
        SC_THREAD( run );
    }
};

#endif