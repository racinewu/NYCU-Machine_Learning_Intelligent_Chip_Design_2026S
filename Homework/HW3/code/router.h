#ifndef ROUTER_H
#define ROUTER_H

#include "systemc.h"
#include <queue>

/*
 * 5-Stage Pipelined Router
 *
 * Stage 1: Sync In    (0.45 ns) - synchronise incoming flit to local clock domain
 * Stage 2: EB         (0.44 ns) - Elastic Buffer, decouple producer from consumer
 * Stage 3: RC         (0.30 ns) - Route Computation, XY routing decision
 * Stage 4: Arbiter+XB (0.46 ns) - arbitrate output port ownership, crossbar transfer
 * Stage 5: Sync Out   (0.45 ns) - drive physical output link
 *
 * Clock = 0.5 ns  (> critical path 0.46 ns)
 *
 * Handshake: 1-cycle per flit on body/tail after header is accepted.
 * The sender holds req high and advances flit every cycle the receiver
 * signals ack, eliminating the idle cycle between flits.
 *
 * Routing: XY (deterministic, deadlock-free)
 * Port map: 0=North 1=South 2=East 3=West 4=Local
 */

SC_MODULE( Router ) {
    sc_in  < bool >  rst;
    sc_in  < bool >  clk;

    sc_out < sc_lv<34> >  out_flit[5];
    sc_out < bool >       out_req[5];
    sc_in  < bool >       in_ack[5];

    sc_in  < sc_lv<34> >  in_flit[5];
    sc_in  < bool >       in_req[5];
    sc_out < bool >       out_ack[5];

    // Stage 1 -> 2: Sync In register (one per port)
    std::queue< sc_lv<34> > sync_in_q[5];

    // Stage 2 -> 3: Elastic Buffer
    std::queue< sc_lv<34> > eb[5];

    // Stage 3 -> 4: Route Computation output (flit + computed target port)
    struct RCEntry { sc_lv<34> flit; int target; };
    std::queue< RCEntry > rc_q[5];

    // Stage 4 -> 5: Crossbar output queues (indexed by output port)
    std::queue< sc_lv<34> > xb_q[5];

    // Stage 5: Sync Out queues
    std::queue< sc_lv<34> > out_q[5];

    // Arbitration state
    int out_owner[5];   // which input port holds each output port (-1 = free)
    int in_target[5];   // which output port each input port is assigned to (-1 = none)

    int router_id;

    void init(int id) { router_id = id; }

    int get_xy_route(int dest_id) {
        int cx = router_id % 4, cy = router_id / 4;
        int dx = dest_id   % 4, dy = dest_id   / 4;
        if (dx > cx) return 2;
        if (dx < cx) return 3;
        if (dy > cy) return 1;
        if (dy < cy) return 0;
        return 4;
    }

    // -------------------------------------------------------
    // Stage 1: Sync In
    // Standard 4-phase handshake matching core TX behaviour:
    // wait req up -> latch flit -> ack up -> wait req down -> ack down
    // -------------------------------------------------------
    void sync_in_logic(int p) {
        while (true) {
            while (in_req[p].read() == 0) wait();
            sc_lv<34> f = in_flit[p].read();
            sync_in_q[p].push(f);
            out_ack[p].write(1);
            while (in_req[p].read() == 1) wait();
            out_ack[p].write(0);
            wait();
        }
    }

    void sync_in_0() { sync_in_logic(0); }
    void sync_in_1() { sync_in_logic(1); }
    void sync_in_2() { sync_in_logic(2); }
    void sync_in_3() { sync_in_logic(3); }
    void sync_in_4() { sync_in_logic(4); }

    // -------------------------------------------------------
    // Stage 2: Elastic Buffer
    // Simply moves flits from sync_in_q to eb each cycle,
    // decoupling the upstream handshake timing from RC.
    // -------------------------------------------------------
    void eb_thread() {
        while (true) {
            for (int i = 0; i < 5; i++) {
                while (!sync_in_q[i].empty()) {
                    eb[i].push(sync_in_q[i].front());
                    sync_in_q[i].pop();
                }
            }
            wait();
        }
    }

    // -------------------------------------------------------
    // Stage 3: Route Computation
    // Reads from EB, computes XY route for header flits,
    // maintains per-port routing state, forwards to rc_q.
    // -------------------------------------------------------
    void rc_thread() {
        while (true) {
            for (int i = 0; i < 5; i++) {
                if (eb[i].empty()) continue;
                sc_lv<34> f = eb[i].front();
                int type = f.range(33, 32).to_uint();

                if (in_target[i] == -1) {
                    if (type == 2) {
                        int dest = f.range(31, 16).to_uint();
                        in_target[i] = get_xy_route(dest);
                    } else {
                        eb[i].pop(); // stray body/tail, discard
                        continue;
                    }
                }
                rc_q[i].push({ f, in_target[i] });
                eb[i].pop();
                if (type == 1) in_target[i] = -1; // release after tail
            }
            wait();
        }
    }

    // -------------------------------------------------------
    // Stage 4: Arbiter + Crossbar
    // Arbitrates output port ownership, then transfers flit
    // from rc_q to the appropriate xb_q.
    // -------------------------------------------------------
    void arbiter_xb_thread() {
        while (true) {
            for (int i = 0; i < 5; i++) {
                if (rc_q[i].empty()) continue;
                RCEntry e = rc_q[i].front();
                int tgt  = e.target;
                int type = e.flit.range(33, 32).to_uint();

                if (out_owner[tgt] == -1) {
                    out_owner[tgt] = i; // acquire
                } else if (out_owner[tgt] != i) {
                    continue; // contended, stall
                }

                xb_q[tgt].push(e.flit);
                rc_q[i].pop();

                if (type == 1) out_owner[tgt] = -1; // release after tail
            }
            wait();
        }
    }

    // -------------------------------------------------------
    // Stage 5: Sync Out
    // Standard 4-phase handshake per flit, matching core RX.
    // req up -> flit on wire -> wait ack up -> req down -> wait ack down
    // -------------------------------------------------------
    void sync_out_logic(int p) {
        while (true) {
            while (out_q[p].empty()) wait();
            sc_lv<34> f = out_q[p].front();
            out_req[p].write(1);
            out_flit[p].write(f);
            while (in_ack[p].read() == 0) wait();
            out_q[p].pop();
            out_req[p].write(0);
            while (in_ack[p].read() == 1) wait();
            wait();
        }
    }

    void sync_out_0() { sync_out_logic(0); }
    void sync_out_1() { sync_out_logic(1); }
    void sync_out_2() { sync_out_logic(2); }
    void sync_out_3() { sync_out_logic(3); }
    void sync_out_4() { sync_out_logic(4); }

    // Crossbar drain: xb_q -> out_q each cycle
    void xb_drain_thread() {
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

    SC_HAS_PROCESS(Router);
    Router(sc_module_name name) : sc_module(name) {
        for (int i = 0; i < 5; i++) {
            out_owner[i] = -1;
            in_target[i] = -1;
        }

        SC_THREAD(sync_in_0); sensitive << clk.pos();
        SC_THREAD(sync_in_1); sensitive << clk.pos();
        SC_THREAD(sync_in_2); sensitive << clk.pos();
        SC_THREAD(sync_in_3); sensitive << clk.pos();
        SC_THREAD(sync_in_4); sensitive << clk.pos();

        SC_THREAD(eb_thread);         sensitive << clk.pos();
        SC_THREAD(rc_thread);         sensitive << clk.pos();
        SC_THREAD(arbiter_xb_thread); sensitive << clk.pos();
        SC_THREAD(xb_drain_thread);   sensitive << clk.pos();

        SC_THREAD(sync_out_0); sensitive << clk.pos();
        SC_THREAD(sync_out_1); sensitive << clk.pos();
        SC_THREAD(sync_out_2); sensitive << clk.pos();
        SC_THREAD(sync_out_3); sensitive << clk.pos();
        SC_THREAD(sync_out_4); sensitive << clk.pos();
    }
};

#endif