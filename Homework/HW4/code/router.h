#ifndef ROUTER_H
#define ROUTER_H

#include "systemc.h"
#include <queue>

using namespace std;

SC_MODULE( Router ) {
    sc_in  < bool >  rst;
    sc_in  < bool >  clk;

    sc_out < sc_lv<34> >  out_flit[5];
    sc_out < bool >       out_req[5];
    sc_in  < bool >       in_ack[5];

    sc_in  < sc_lv<34> >  in_flit[5];
    sc_in  < bool >       in_req[5];
    sc_out < bool >       out_ack[5];

    std::queue< sc_lv<34> > sync_in_q[5];
    std::queue< sc_lv<34> > eb[5];
    struct RCEntry { sc_lv<34> flit; int target; };
    std::queue< RCEntry > rc_q[5];
    std::queue< sc_lv<34> > xb_q[5];
    std::queue< sc_lv<34> > out_q[5];

    int out_owner[5];
    int in_target[5];
    int router_id;

    void init(int id) { router_id = id; }

    int get_xy_route(int dest_id) {
        int cx = router_id % 4, cy = router_id / 4;
        int dx = dest_id   % 4, dy = dest_id   / 4;
        if (dx > cx) return 2; // East
        if (dx < cx) return 3; // West
        if (dy > cy) return 1; // South
        if (dy < cy) return 0; // North
        return 4;              // Local
    }

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

    void eb_thread() {
        while (true) {
            for (int i=0; i<5; i++)
                while (!sync_in_q[i].empty()) { eb[i].push(sync_in_q[i].front()); sync_in_q[i].pop(); }
            wait();
        }
    }

    void rc_thread() {
        while (true) {
            for (int i=0; i<5; i++) {
                if (eb[i].empty()) continue;
                sc_lv<34> f = eb[i].front();
                int type = f.range(33,32).to_uint();
                if (in_target[i] == -1) {
                    if (type == 2) {
                        int dest = f.range(31,16).to_uint();
                        in_target[i] = get_xy_route(dest);
                    } else { eb[i].pop(); continue; }
                }
                rc_q[i].push({ f, in_target[i] });
                eb[i].pop();
                if (type == 1) in_target[i] = -1;
            }
            wait();
        }
    }

    void arbiter_xb_thread() {
        while (true) {
            for (int i=0; i<5; i++) {
                if (rc_q[i].empty()) continue;
                RCEntry e = rc_q[i].front();
                int tgt  = e.target;
                int type = e.flit.range(33,32).to_uint();
                if (out_owner[tgt] == -1) out_owner[tgt] = i;
                else if (out_owner[tgt] != i) continue;
                xb_q[tgt].push(e.flit);
                rc_q[i].pop();
                if (type == 1) out_owner[tgt] = -1;
            }
            wait();
        }
    }

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

    void xb_drain_thread() {
        while (true) {
            for (int p=0; p<5; p++)
                while (!xb_q[p].empty()) { out_q[p].push(xb_q[p].front()); xb_q[p].pop(); }
            wait();
        }
    }

    SC_HAS_PROCESS(Router);
    Router(sc_module_name name) : sc_module(name) {
        for (int i=0; i<5; i++) { out_owner[i]=-1; in_target[i]=-1; }
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