#ifndef AXI_DMA_H
#define AXI_DMA_H

// ==================================================
// AXI4-based DMA Model (Baseline)
//
// Implements AXI4 master with 5 independent channels.
// All required signals are present as sc_signal members,
// modeling the actual AXI handshake behavior.
//
// Supported signals (per TA requirement):
//   AR channel: ARADDR, ARLEN, ARVALID, ARREADY
//   R  channel: RDATA,  RLAST, RVALID,  RREADY
//   AW channel: AWADDR, AWLEN, AWVALID, AWREADY
//   W  channel: WDATA,  WLAST, WVALID,  WREADY
//   B  channel: BRESP,  BVALID, BREADY
//
// Additional signals included for completeness:
//   ARSIZE, ARBURST, AWSIZE, AWBURST, RRESP
//
// Handshake protocol:
//   Transfer occurs when both VALID and READY are high
//   in the same cycle. VALID is driven by initiator;
//   READY is driven by receiver.
//   Rule: VALID must not wait for READY (AXI deadlock rule).
//
// Burst type : INCR (ARBURST/AWBURST = 01)
// Data width : 32 bits per beat (1 float)
// Beat size  : ARSIZE/AWSIZE = 010 (4 bytes)
// Burst len  : ARLEN/AWLEN   = n_beats - 1
//
// Abstraction level:
//   Behavioral (not gate-level). All sc_signal transitions
//   are modeled correctly per AXI spec. No clock sensitivity
//   - signals are driven synchronously within read()/write()
//   calls from the controller's SC_THREAD.
//
// Outstanding transactions: NOT supported (baseline).
// ==================================================

#include "dram.h"
#include "systemc.h"
#include <vector>
#include <cassert>
#include <iostream>
#include <cstring>
#include <sstream>
#include <iomanip>

// AXI constants
#define AXI_BURST_INCR  0x1u
#define AXI_RESP_OKAY   0x0u
#define AXI_SIZE_4B     0x2u   // 2^2 = 4 bytes per beat

SC_MODULE(AXI4_DMA) {

    // ==================================================
    // AR Channel (Read Address) - master drives, slave accepts
    // ==================================================
    sc_signal<unsigned int> ARADDR;
    sc_signal<unsigned int> ARLEN;    // burst length - 1
    sc_signal<unsigned int> ARSIZE;   // 010 = 4 bytes/beat
    sc_signal<unsigned int> ARBURST;  // 01 = INCR
    sc_signal<bool>         ARVALID;  // master asserts
    sc_signal<bool>         ARREADY;  // slave asserts

    // ==================================================
    // R Channel (Read Data) - slave drives, master accepts
    // ==================================================
    sc_signal<float>        RDATA;
    sc_signal<bool>         RLAST;
    sc_signal<bool>         RVALID;   // slave asserts
    sc_signal<bool>         RREADY;   // master asserts
    sc_signal<unsigned int> RRESP;    // 00 = OKAY

    // ==================================================
    // AW Channel (Write Address) - master drives, slave accepts
    // ==================================================
    sc_signal<unsigned int> AWADDR;
    sc_signal<unsigned int> AWLEN;
    sc_signal<unsigned int> AWSIZE;
    sc_signal<unsigned int> AWBURST;
    sc_signal<bool>         AWVALID;
    sc_signal<bool>         AWREADY;

    // ==================================================
    // W Channel (Write Data) - master drives, slave accepts
    // ==================================================
    sc_signal<float>        WDATA;
    sc_signal<bool>         WLAST;
    sc_signal<bool>         WVALID;
    sc_signal<bool>         WREADY;

    // ==================================================
    // B Channel (Write Response) - slave drives, master accepts
    // ==================================================
    sc_signal<unsigned int> BRESP;
    sc_signal<bool>         BVALID;
    sc_signal<bool>         BREADY;

    // ==================================================
    // Statistics
    // ==================================================
    long long ar_count_  = 0;
    long long r_beats_   = 0;
    long long aw_count_  = 0;
    long long w_beats_   = 0;
    long long b_count_   = 0;

    DRAM* dram_;

    // ==================================================
    // AXI Read Transaction
    //
    // AR phase:
    //   Master sets ARADDR, ARLEN, ARSIZE, ARBURST, ARVALID=1
    //   Slave asserts ARREADY=1 (immediately in this model)
    //   Handshake: both high -> deassert ARVALID
    //
    // R phase (n_beats iterations):
    //   Slave sets RDATA, RVALID=1, RLAST=1 on last beat
    //   Master asserts RREADY=1
    //   Handshake: both high -> data transferred
    //   Slave deasserts RVALID after each beat
    // ==================================================
    std::vector<float> read(unsigned int addr, int n) {
        assert(n > 0);

        // AR Channel: master drives
        ARADDR.write(addr);
        ARLEN.write((unsigned)(n - 1));
        ARSIZE.write(AXI_SIZE_4B);
        ARBURST.write(AXI_BURST_INCR);
        ARVALID.write(true);

        // Slave asserts ARREADY (ready immediately)
        ARREADY.write(true);
        // Handshake complete (ARVALID & ARREADY both high)
        ar_count_++;
        // Deassert
        ARVALID.write(false);
        ARREADY.write(false);

        // R Channel: slave drives data beats
        RREADY.write(true);   // master always ready
        RRESP.write(AXI_RESP_OKAY);

        std::vector<float> result(n);
        for (int beat = 0; beat < n; beat++) {
            unsigned int baddr = addr + (unsigned)(beat * 4);
            float val;
            dram_->read_floats(baddr, &val, 1);

            RDATA.write(val);
            RLAST.write(beat == n - 1);
            RVALID.write(true);
            // Handshake: RVALID & RREADY both high -> transfer
            result[beat] = val;
            r_beats_++;
            // Deassert after beat
            RVALID.write(false);
            RLAST.write(false);
        }
        RREADY.write(false);

        return result;
    }

    void read_to(unsigned int addr, float* out, int n) {
        auto v = read(addr, n);
        std::memcpy(out, v.data(), (size_t)n * sizeof(float));
    }

    // ==================================================
    // AXI Write Transaction
    //
    // AW phase:
    //   Master sets AWADDR, AWLEN, AWSIZE, AWBURST, AWVALID=1
    //   Slave asserts AWREADY=1
    //   Handshake -> deassert AWVALID
    //
    // W phase (n_beats iterations):
    //   Master sets WDATA, WLAST (on last beat), WVALID=1
    //   Slave asserts WREADY=1
    //   Handshake -> data written to DRAM
    //
    // B phase:
    //   Slave sets BRESP=OKAY, BVALID=1
    //   Master asserts BREADY=1
    //   Handshake -> transaction complete
    // ==================================================
    void write(unsigned int addr, const float* data, int n) {
        assert(n > 0);

        // AW Channel
        AWADDR.write(addr);
        AWLEN.write((unsigned)(n - 1));
        AWSIZE.write(AXI_SIZE_4B);
        AWBURST.write(AXI_BURST_INCR);
        AWVALID.write(true);
        AWREADY.write(true);   // slave ready immediately
        aw_count_++;
        AWVALID.write(false);
        AWREADY.write(false);

        // W Channel
        WREADY.write(true);    // slave always ready
        for (int beat = 0; beat < n; beat++) {
            unsigned int baddr = addr + (unsigned)(beat * 4);
            WDATA.write(data[beat]);
            WLAST.write(beat == n - 1);
            WVALID.write(true);
            // Handshake: WVALID & WREADY both high -> write
            dram_->write_floats(baddr, &data[beat], 1);
            w_beats_++;
            WVALID.write(false);
            WLAST.write(false);
        }
        WREADY.write(false);

        // B Channel
        BRESP.write(AXI_RESP_OKAY);
        BVALID.write(true);
        BREADY.write(true);    // master accepts response
        b_count_++;
        BVALID.write(false);
        BREADY.write(false);
    }

    void write(unsigned int addr, const std::vector<float>& v) {
        write(addr, v.data(), (int)v.size());
    }

    // ==================================================
    // Statistics
    // ==================================================
    void reset_stats() {
        ar_count_ = r_beats_ = aw_count_ = w_beats_ = b_count_ = 0;
    }
    long long total_read_beats()  const { return r_beats_; }
    long long total_write_beats() const { return w_beats_; }

    void print_stats() const {
        auto fmt = [](long long beats) -> std::string {
            long long bytes = beats * 4;
            std::ostringstream s;
            s << std::fixed << std::setprecision(2);
            if (bytes >= 1024*1024)      s << bytes/1024.0/1024.0 << " MB";
            else if (bytes >= 1024)      s << bytes/1024.0        << " KB";
            else                         s << bytes               << " B";
            return s.str();
        };
        std::cout << "[AXI]  AR transactions : " << ar_count_ << std::endl;
        std::cout << "[AXI]  R  data beats   : " << r_beats_
                  << " (" << fmt(r_beats_) << ")" << std::endl;
        std::cout << "[AXI]  AW transactions : " << aw_count_ << std::endl;
        std::cout << "[AXI]  W  data beats   : " << w_beats_
                  << " (" << fmt(w_beats_) << ")" << std::endl;
        std::cout << "[AXI]  B  responses    : " << b_count_  << std::endl;
        double avg_r = (ar_count_ > 0) ? (double)r_beats_ / ar_count_ : 0.0;
        double avg_w = (aw_count_ > 0) ? (double)w_beats_ / aw_count_ : 0.0;
        std::cout << std::fixed << std::setprecision(1);
        std::cout << "[AXI]  Avg read  ARLEN+1=" << avg_r << " beats"
                  << " | Avg write AWLEN+1=" << avg_w << " beats" << std::endl;
    }

    SC_HAS_PROCESS(AXI4_DMA);
    explicit AXI4_DMA(sc_module_name name, DRAM* dram)
        : sc_module(name), dram_(dram) {
        // Initialize all signals to idle state
        ARVALID.write(false); ARREADY.write(false);
        RVALID.write(false);  RREADY.write(false);  RLAST.write(false);
        RRESP.write(AXI_RESP_OKAY);
        AWVALID.write(false); AWREADY.write(false);
        WVALID.write(false);  WREADY.write(false);  WLAST.write(false);
        BVALID.write(false);  BREADY.write(false);
        BRESP.write(AXI_RESP_OKAY);
        ARSIZE.write(AXI_SIZE_4B);   ARBURST.write(AXI_BURST_INCR);
        AWSIZE.write(AXI_SIZE_4B);   AWBURST.write(AXI_BURST_INCR);
    }
};

#endif // AXI_DMA_H
