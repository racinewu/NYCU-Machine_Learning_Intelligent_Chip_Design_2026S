#ifndef FORWARDER_ADDR_SC_THREAD_V2_H
#define FORWARDER_ADDR_SC_THREAD_V2_H

#include <systemc.h>
#include <iostream>
#include <vector>

SC_MODULE(Forwarder)
{
    // Ports
    sc_in<bool> ACLK;
    sc_in<bool> ARESETn; // Active-low reset

    // --- Request Interface (Master to Producer) ---
    sc_out<unsigned int> REQ_ID_TO_PROD;
    sc_out<bool> REQ_VALID_TO_PROD;
    sc_in<bool> REQ_READY_FROM_PROD;

    // --- AXI Stream Data Interface (Slave from Producer) ---
    sc_in<bool> S_AXIS_TVALID;
    sc_out<bool> S_AXIS_TREADY;
    sc_in<char> S_AXIS_TDATA;
    sc_in<bool> S_AXIS_TLAST;

    // --- AXI Stream Data Interface (Master to Checker) ---
    sc_out<bool> M_AXIS_TVALID;
    sc_in<bool> M_AXIS_TREADY;
    sc_out<char> M_AXIS_TDATA;
    sc_out<bool> M_AXIS_TLAST;

    const unsigned int TOTAL_STRINGS_TO_REQUEST; // Define how many strings to request

    // hint : You can directly pull S_AXIS_TREADY to 1
    // hint : You can directly set REQ_ID_TO_PROD to 0
    void forwarder_thread_logic()
    {
        REQ_ID_TO_PROD.write(0);
        REQ_VALID_TO_PROD.write(false);
        S_AXIS_TREADY.write(true);
        M_AXIS_TVALID.write(false);
        M_AXIS_TDATA.write('\0');
        M_AXIS_TLAST.write(false);
 
        // Wait for two clock edges until the reset signal is fully deasserted.
        wait(); // skip delta, land on first real clock edge (reset=0)
        wait(); // now reset should be gone (=1)
        while (!ARESETn.read()) { wait(); } // Poll in case reset lasts longer
 
        for (unsigned int str_idx = 0; str_idx < TOTAL_STRINGS_TO_REQUEST; str_idx++)
        {
            // Step 1: Send Read Address Request
            REQ_VALID_TO_PROD.write(true);
            while (!REQ_READY_FROM_PROD.read()) { wait(); } // Wait for REQ_READY handshake
            wait(); // Handshake done this cycle, deassert next cycle
            REQ_VALID_TO_PROD.write(false);
 
            // Step 2: Receive data and forward to Checker
            do
            {
                while (!S_AXIS_TVALID.read()) { wait(); }

                M_AXIS_TVALID.write(true);
                M_AXIS_TDATA.write(S_AXIS_TDATA.read());
                M_AXIS_TLAST.write(S_AXIS_TLAST.read());

                while (!M_AXIS_TREADY.read()) { wait(); }
                wait();
                M_AXIS_TVALID.write(false);

            } while (!M_AXIS_TLAST.read()); // Read back the written value
        }
 
        // std::cout << sc_time_stamp() << ": Forwarder: Done." << std::endl;
        while (true) { wait(); }
    }

    // if you need to add more functions, please add them here

    SC_CTOR(Forwarder) : TOTAL_STRINGS_TO_REQUEST(1) // Initialize const member in initializer list
    {
        SC_THREAD(forwarder_thread_logic);
        sensitive << ACLK.pos();
        sensitive << ARESETn.neg();
    }
};

#endif // FORWARDER_ADDR_SC_THREAD_V2_H