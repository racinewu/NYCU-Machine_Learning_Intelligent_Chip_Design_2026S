#ifndef AXI_DMA_H
#define AXI_DMA_H

// ============================================================
// AXI4-based DMA Model (Optimized)
//
// Two independent AXI4 master ports:
//   Port 0 (main):    used by logic_thread    — Conv/FC normal DMA
//   Port 1 (prefetch): used by prefetch_thread — FC weight tile prefetch
//
// Each port has its own complete set of 5-channel AXI signals,
// eliminating the SC_MANY_WRITERS conflict when both threads
// access DRAM concurrently.
//
// Both ports share the same DRAM model (slave), which supports
// concurrent read access from different addresses.
//
// Signals per port (TA required set):
//   AR: ARADDR, ARLEN, ARVALID, ARREADY
//   R:  RDATA,  RLAST, RVALID,  RREADY,  RRESP
//   AW: AWADDR, AWLEN, AWVALID, AWREADY
//   W:  WDATA,  WLAST, WVALID,  WREADY
//   B:  BRESP,  BVALID, BREADY
// Additional: ARSIZE, ARBURST, AWSIZE, AWBURST
//
// Burst type: INCR (ARBURST/AWBURST = 01)
// Beat size : ARSIZE/AWSIZE = 010 (4 bytes)
// Outstanding: not supported (baseline behavior per port)
// ============================================================

#include "dram.h"
#include "systemc.h"
#include <vector>
#include <cassert>
#include <iostream>
#include <cstring>
#include <sstream>
#include <iomanip>

#define AXI_BURST_INCR  0x1u
#define AXI_RESP_OKAY   0x0u
#define AXI_SIZE_4B     0x2u

// ============================================================
// AXI4_Port: one complete set of AXI4 channels (one master port)
// ============================================================
struct AXI4_Port {
    // AR channel
    sc_signal<unsigned int> ARADDR;
    sc_signal<unsigned int> ARLEN;
    sc_signal<unsigned int> ARSIZE;
    sc_signal<unsigned int> ARBURST;
    sc_signal<bool>         ARVALID;
    sc_signal<bool>         ARREADY;
    // R channel
    sc_signal<float>        RDATA;
    sc_signal<bool>         RLAST;
    sc_signal<bool>         RVALID;
    sc_signal<bool>         RREADY;
    sc_signal<unsigned int> RRESP;
    // AW channel
    sc_signal<unsigned int> AWADDR;
    sc_signal<unsigned int> AWLEN;
    sc_signal<unsigned int> AWSIZE;
    sc_signal<unsigned int> AWBURST;
    sc_signal<bool>         AWVALID;
    sc_signal<bool>         AWREADY;
    // W channel
    sc_signal<float>        WDATA;
    sc_signal<bool>         WLAST;
    sc_signal<bool>         WVALID;
    sc_signal<bool>         WREADY;
    // B channel
    sc_signal<unsigned int> BRESP;
    sc_signal<bool>         BVALID;
    sc_signal<bool>         BREADY;

    void init_idle() {
        ARVALID.write(false); ARREADY.write(false);
        RVALID.write(false);  RREADY.write(false); RLAST.write(false);
        RRESP.write(AXI_RESP_OKAY);
        AWVALID.write(false); AWREADY.write(false);
        WVALID.write(false);  WREADY.write(false); WLAST.write(false);
        BVALID.write(false);  BREADY.write(false);
        BRESP.write(AXI_RESP_OKAY);
        ARSIZE.write(AXI_SIZE_4B);  ARBURST.write(AXI_BURST_INCR);
        AWSIZE.write(AXI_SIZE_4B);  AWBURST.write(AXI_BURST_INCR);
    }

    // AXI read transaction via this port
    std::vector<float> read(DRAM* dram, unsigned int addr, int n,
                            long long& ar_count, long long& r_beats) {
        assert(n > 0);
        // AR handshake
        ARADDR.write(addr);
        ARLEN.write((unsigned)(n - 1));
        ARSIZE.write(AXI_SIZE_4B);
        ARBURST.write(AXI_BURST_INCR);
        ARVALID.write(true);
        ARREADY.write(true);
        ar_count++;
        ARVALID.write(false); ARREADY.write(false);
        // R beats
        RREADY.write(true);
        RRESP.write(AXI_RESP_OKAY);
        std::vector<float> result(n);
        for (int beat = 0; beat < n; beat++) {
            float val;
            dram->read_floats(addr + (unsigned)(beat * 4), &val, 1);
            RDATA.write(val);
            RLAST.write(beat == n - 1);
            RVALID.write(true);
            result[beat] = val;
            r_beats++;
            RVALID.write(false);
            RLAST.write(false);
        }
        RREADY.write(false);
        return result;
    }

    // AXI write transaction via this port
    void write(DRAM* dram, unsigned int addr, const float* data, int n,
               long long& aw_count, long long& w_beats, long long& b_count) {
        assert(n > 0);
        // AW handshake
        AWADDR.write(addr);
        AWLEN.write((unsigned)(n - 1));
        AWSIZE.write(AXI_SIZE_4B);
        AWBURST.write(AXI_BURST_INCR);
        AWVALID.write(true);
        AWREADY.write(true);
        aw_count++;
        AWVALID.write(false); AWREADY.write(false);
        // W beats
        WREADY.write(true);
        for (int beat = 0; beat < n; beat++) {
            WDATA.write(data[beat]);
            WLAST.write(beat == n - 1);
            WVALID.write(true);
            dram->write_floats(addr + (unsigned)(beat * 4), &data[beat], 1);
            w_beats++;
            WVALID.write(false);
            WLAST.write(false);
        }
        WREADY.write(false);
        // B response
        BRESP.write(AXI_RESP_OKAY);
        BVALID.write(true);
        BREADY.write(true);
        b_count++;
        BVALID.write(false); BREADY.write(false);
    }
};

// ============================================================
// AXI4_DMA: two independent ports (main + prefetch)
// ============================================================
SC_MODULE(AXI4_DMA) {

    // Port 0: main (logic_thread)
    AXI4_Port port0;
    // Port 1: prefetch (prefetch_thread)
    AXI4_Port port1;

    DRAM* dram_;

    // Per-port statistics
    long long p0_ar = 0, p0_r = 0, p0_aw = 0, p0_w = 0, p0_b = 0;
    long long p1_ar = 0, p1_r = 0;  // prefetch port: read only

    // -------------------------------------------------------
    // Port 0 interface (logic_thread)
    // -------------------------------------------------------
    std::vector<float> read(unsigned int addr, int n) {
        return port0.read(dram_, addr, n, p0_ar, p0_r);
    }
    void write(unsigned int addr, const float* data, int n) {
        port0.write(dram_, addr, data, n, p0_aw, p0_w, p0_b);
    }
    void write(unsigned int addr, const std::vector<float>& v) {
        write(addr, v.data(), (int)v.size());
    }

    // -------------------------------------------------------
    // Port 1 interface (prefetch_thread) — read only
    // -------------------------------------------------------
    std::vector<float> prefetch_read(unsigned int addr, int n) {
        return port1.read(dram_, addr, n, p1_ar, p1_r);
    }

    // -------------------------------------------------------
    // Statistics
    // -------------------------------------------------------
    void reset_stats() {
        p0_ar = p0_r = p0_aw = p0_w = p0_b = 0;
        p1_ar = p1_r = 0;
    }
    long long total_read_beats()  const { return p0_r + p1_r; }
    long long total_write_beats() const { return p0_w; }

    void print_stats() const {
        auto fmt = [](long long beats) -> std::string {
            long long bytes = beats * 4;
            std::ostringstream s; s << std::fixed << std::setprecision(2);
            if (bytes >= 1024*1024)      s << bytes/1024.0/1024.0 << " MB";
            else if (bytes >= 1024)      s << bytes/1024.0        << " KB";
            else                         s << bytes               << " B";
            return s.str();
        };
        std::cout << "[AXI Port0] AR=" << p0_ar
                  << " R=" << p0_r << " (" << fmt(p0_r) << ")"
                  << " AW=" << p0_aw
                  << " W=" << p0_w << " (" << fmt(p0_w) << ")"
                  << " B=" << p0_b << std::endl;
        std::cout << "[AXI Port1] AR=" << p1_ar
                  << " R=" << p1_r << " (" << fmt(p1_r) << ")"
                  << " (prefetch port, read-only)" << std::endl;
        std::cout << "[AXI Total] R=" << (p0_r+p1_r)
                  << " (" << fmt(p0_r+p1_r) << ")"
                  << " W=" << p0_w
                  << " (" << fmt(p0_w) << ")" << std::endl;
    }

    SC_HAS_PROCESS(AXI4_DMA);
    explicit AXI4_DMA(sc_module_name name, DRAM* dram)
        : sc_module(name), dram_(dram) {
        port0.init_idle();
        port1.init_idle();
    }
};

#endif // AXI_DMA_H
