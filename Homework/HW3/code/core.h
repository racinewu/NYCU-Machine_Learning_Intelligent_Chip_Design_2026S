#ifndef CORE_H
#define CORE_H

#include "systemc.h"
#include "pe.h"

SC_MODULE( Core ) {
    sc_in  < bool >  rst;
    sc_in  < bool >  clk;
    // receive
    sc_in  < sc_lv<34> > flit_rx;
    sc_in  < bool > req_rx;
    sc_out < bool > ack_rx;
    // transmit
    sc_out < sc_lv<34> > flit_tx;
    sc_out < bool > req_tx;
    sc_in  < bool > ack_tx;

    PE pe;
    int core_id;

    void init(int id) {
        core_id = id;
        pe.init(id);
    }

    // Standard 4-phase handshake: req up -> wait ack up -> req down -> wait ack down
    // Applied uniformly to every flit (header, body, tail).
    // This is the safest approach given the req/ack port interface.
    void send_flit(sc_lv<34> f) {
        req_tx.write(1);
        flit_tx.write(f);
        while (ack_tx.read() == 0) wait();
        req_tx.write(0);
        while (ack_tx.read() == 1) wait();
    }

    void tx_thread() {
        while (true) {
            Packet* p = pe.get_packet();
            if (p != NULL) {
                // Header flit
                sc_lv<34> h;
                h.range(33, 32) = 2;
                h.range(31, 16) = p->dest_id;
                h.range(15,  0) = p->source_id;
                send_flit(h);

                // Body and Tail flits
                for (size_t i = 0; i < p->datas.size(); i++) {
                    sc_lv<34> f;
                    f.range(33, 32) = (i == p->datas.size() - 1) ? 1 : 0;
                    union { float fval; unsigned int ival; } cv;
                    cv.fval = p->datas[i];
                    f.range(31, 0) = cv.ival;
                    send_flit(f);
                }
                delete p;
            }
            wait();
        }
    }

    void rx_thread() {
        Packet* p = NULL;
        while (true) {
            while (req_rx.read() == 0) wait();
            sc_lv<34> f = flit_rx.read();

            ack_rx.write(1);
            while (req_rx.read() == 1) wait();
            ack_rx.write(0);

            unsigned int type = f.range(33, 32).to_uint();
            if (type == 2) {
                p = new Packet();
                p->dest_id   = f.range(31, 16).to_uint();
                p->source_id = f.range(15,  0).to_uint();
            } else if (type == 0 || type == 1) {
                union { float fval; unsigned int ival; } cv;
                cv.ival = f.range(31, 0).to_uint();
                if (p != NULL) {
                    p->datas.push_back(cv.fval);
                    if (type == 1) {
                        pe.check_packet(p);
                        delete p;
                        p = NULL;
                    }
                }
            }
            wait();
        }
    }

    SC_HAS_PROCESS(Core);
    Core(sc_module_name name) : sc_module(name) {
        SC_THREAD(tx_thread);
        sensitive << clk.pos();
        SC_THREAD(rx_thread);
        sensitive << clk.pos();
    }
};

#endif