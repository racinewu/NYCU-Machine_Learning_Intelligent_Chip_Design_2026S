#ifndef ROUTER_H
#define ROUTER_H

#include "systemc.h"
#include <queue>

/*
 * Router Design: 3-Stage Pipelined Router
 *
 * Pipeline Stages (based on Router_Function_Delay.pptx):
 *   Stage 1: Sync In (0.45ns) + EB / Elastic Buffer (0.44ns) = 0.89ns
 *   Stage 2: RC / Route Computation (0.30ns) + Arbiter (0.23ns) = 0.53ns
 *   Stage 3: XB / Crossbar (0.23ns) + Sync Out (0.45ns) = 0.68ns
 *
 * Critical path = max(0.89, 0.53, 0.68) = 0.89ns
 * Clock period = 1ns (satisfies all stage constraints with margin)
 *
 * Routing Algorithm: XY Routing (deterministic, deadlock-free)
 *   - First route along X axis (East/West), then Y axis (North/South)
 *
 * Port mapping:
 *   0 = North, 1 = South, 2 = East, 3 = West, 4 = Local (Core)
 *
 * Buffer depth: 4 flits per input port
 * Virtual Channels: Not used
 */

SC_MODULE( Router ) {
    sc_in  < bool >  rst;
    sc_in  < bool >  clk;

    sc_out < sc_lv<34> >  out_flit[5];
    sc_out < bool >  out_req[5];
    sc_in  < bool >  in_ack[5];

    sc_in  < sc_lv<34> >  in_flit[5];
    sc_in  < bool >  in_req[5];
    sc_out < bool >  out_ack[5];

    // Stage 1: Elastic Buffers (EB) - one per input port
    std::queue< sc_lv<34> > eb[5];

    // Stage 2 -> Stage 3 crossbar queues (one per output port)
    std::queue< sc_lv<34> > xb_q[5];

    // Arbitration: which input port owns each output port (-1 = free)
    int out_owner[5];
    // Which output port each input port is routed to (-1 = unassigned)
    int in_target[5];

    // Stage 3: Output queues (post-crossbar, pre-Sync Out)
    std::queue< sc_lv<34> > out_q[5];

    int router_id;

    void init(int id) {
        router_id = id;
    }

    // XY Routing: route along X axis first, then Y axis
    int get_xy_route(int dest_id) {
        int cx = router_id % 4;
        int cy = router_id / 4;
        int dx = dest_id % 4;
        int dy = dest_id / 4;

        if (dx > cx) return 2; // East
        if (dx < cx) return 3; // West
        if (dy > cy) return 1; // South
        if (dy < cy) return 0; // North
        return 4;              // Local (arrived at destination)
    }

    // Stage 1: Sync In + Elastic Buffer (one thread per input port)
    void rx_logic(int p) {
        while (true) {
            while (in_req[p].read() == 0) wait();
            sc_lv<34> f = in_flit[p].read();
            eb[p].push(f);

            out_ack[p].write(1);
            while (in_req[p].read() == 1) wait();
            out_ack[p].write(0);
            wait();
        }
    }

    void rx_thread_0() { rx_logic(0); }
    void rx_thread_1() { rx_logic(1); }
    void rx_thread_2() { rx_logic(2); }
    void rx_thread_3() { rx_logic(3); }
    void rx_thread_4() { rx_logic(4); }

    // Stage 2: Route Computation + Arbiter (one thread for all ports)
    void route_arbiter_thread() {
        while (true) {
            for (int i = 0; i < 5; i++) {
                if (eb[i].empty()) continue;

                sc_lv<34> f = eb[i].front();
                int type = f.range(33, 32).to_uint();

                if (in_target[i] == -1) {
                    if (type == 2) { // Header: run RC and try to acquire output port
                        int dest = f.range(31, 16).to_uint();
                        int target = get_xy_route(dest);

                        if (out_owner[target] == -1) {
                            out_owner[target] = i;
                            in_target[i] = target;
                        } else {
                            continue; // Output port busy, stall
                        }
                    } else {
                        // Stray Body/Tail without a Header: discard
                        eb[i].pop();
                        continue;
                    }
                }

                // Forward flit through crossbar to target output queue
                int tgt = in_target[i];
                xb_q[tgt].push(f);
                eb[i].pop();

                // Release output port lock on Tail flit
                if (type == 1) {
                    out_owner[tgt] = -1;
                    in_target[i] = -1;
                }
            }
            wait();
        }
    }

    // Stage 3a: Crossbar transfer (move from xb_q to out_q each cycle)
    void crossbar_thread() {
        while (true) {
            for (int p = 0; p < 5; p++) {
                while (!xb_q[p].empty()) {
                    out_q[p].push(xb_q[p].front());
                    xb_q[p].pop();
                }
            }
            wait();
        }
    }

    // Stage 3b: Sync Out (one thread per output port)
    void tx_logic(int p) {
        while (true) {
            if (!out_q[p].empty()) {
                sc_lv<34> f = out_q[p].front();
                out_req[p].write(1);
                out_flit[p].write(f);

                while (in_ack[p].read() == 0) wait();
                out_q[p].pop();
                out_req[p].write(0);
                while (in_ack[p].read() == 1) wait();
            }
            wait();
        }
    }

    void tx_thread_0() { tx_logic(0); }
    void tx_thread_1() { tx_logic(1); }
    void tx_thread_2() { tx_logic(2); }
    void tx_thread_3() { tx_logic(3); }
    void tx_thread_4() { tx_logic(4); }

    SC_HAS_PROCESS(Router);
    Router(sc_module_name name) : sc_module(name) {
        for (int i = 0; i < 5; i++) {
            out_owner[i] = -1;
            in_target[i] = -1;
        }

        SC_THREAD(rx_thread_0); sensitive << clk.pos();
        SC_THREAD(rx_thread_1); sensitive << clk.pos();
        SC_THREAD(rx_thread_2); sensitive << clk.pos();
        SC_THREAD(rx_thread_3); sensitive << clk.pos();
        SC_THREAD(rx_thread_4); sensitive << clk.pos();

        SC_THREAD(route_arbiter_thread); sensitive << clk.pos();
        SC_THREAD(crossbar_thread);      sensitive << clk.pos();

        SC_THREAD(tx_thread_0); sensitive << clk.pos();
        SC_THREAD(tx_thread_1); sensitive << clk.pos();
        SC_THREAD(tx_thread_2); sensitive << clk.pos();
        SC_THREAD(tx_thread_3); sensitive << clk.pos();
        SC_THREAD(tx_thread_4); sensitive << clk.pos();
    }
};

#endif