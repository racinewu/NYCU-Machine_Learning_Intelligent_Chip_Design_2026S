#ifndef CONTROLLER_H
#define CONTROLLER_H

// ============================================================
// FP Baseline Controller
//
// Changes from new HW4 controller:
//   1. ROM signals removed (layer_id, layer_id_type,
//      layer_id_valid, data, data_valid).
//   2. rom_read() replaced by dma_read() via AXI4_DMA -> DRAM.
//   3. On-chip SRAM used as staging buffer between DMA and PE.
//   4. Data folder only accessed in init_dram() before sc_start.
//   5. Output results written back to DRAM before printing.
//   6. No set_cores() / no direct core pointer access.
//      FC weights sent via real NoC PKT_FC_W (same as new HW4).
//   7. Execution cycle counter added for report metrics.
// ============================================================

#include "systemc.h"
#include "noc_io.h"
#include "config.h"
#include "dram.h"
#include "axi_dma.h"
#include "sram.h"
#include <vector>
#include <queue>
#include <map>
#include <string>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <cmath>
#include <sstream>

SC_MODULE(Controller) {
    sc_in  <bool>              rst;
    sc_in  <bool>              clk;
    sc_out <sc_lv<FLIT_WIDTH>> flit_tx;
    sc_out <bool>              req_tx;
    sc_in  <bool>              ack_tx;
    sc_in  <sc_lv<FLIT_WIDTH>> flit_rx;
    sc_in  <bool>              req_rx;
    sc_out <bool>              ack_rx;

    // -------------------------------------------------------
    // Memory subsystem
    // -------------------------------------------------------
    DRAM*     dram_;
    AXI4_DMA* dma_;    // SC_MODULE, owned by controller
    SRAM*     sram_;   // 2 MB on-chip staging buffer

    // Core pointers for post-run stats collection only
    // (no compute bypass — weights still sent via NoC PKT_FC_W)
    Core* cores_[16] = {};
    void set_cores(Core** c) { for (int i = 0; i < 16; i++) cores_[i] = c[i]; }

    // Execution cycle counter
    long long exec_cycles_ = 0;

    // -------------------------------------------------------
    // TX / RX infrastructure (identical to new HW4)
    // -------------------------------------------------------
    std::queue<Packet*> tx_q;
    sc_event            tx_ready;
    std::queue<Packet*> rx_q;
    sc_event            rx_arrived;

    void tx_thread() {
        req_tx.write(0); flit_tx.write(0);
        while (true) {
            if (tx_q.empty()) wait(tx_ready);
            if (tx_q.empty()) continue;
            Packet* p = tx_q.front(); tx_q.pop();
            noc_send_packet(p->dest_id, CTRL_ID,
                            p->pkt_type, p->ch_start, p->tile_idx,
                            p->datas, flit_tx, req_tx, ack_tx);
            delete p;
        }
    }

    void rx_thread() {
        ack_rx.write(0);
        while (true) {
            Packet* p = noc_recv_packet(flit_rx, req_rx, ack_rx);
            if (p) { rx_q.push(p); rx_arrived.notify(); }
        }
    }

    void enqueue(int dest, int pkt_type, int ch_start,
                 const std::vector<float>& datas) {
        int total = (int)datas.size();
        if (total == 0) {
            Packet* p = new Packet();
            p->dest_id  = dest; p->pkt_type  = pkt_type;
            p->ch_start = ch_start; p->tile_idx = 0;
            tx_q.push(p); tx_ready.notify(); return;
        }
        for (int off = 0, tidx = 0; off < total; off += TILE_SIZE, tidx++) {
            Packet* p = new Packet();
            p->dest_id  = dest; p->pkt_type  = pkt_type;
            p->ch_start = ch_start; p->tile_idx = tidx;
            int end = std::min(off + TILE_SIZE, total);
            p->datas.insert(p->datas.end(),
                            datas.begin() + off, datas.begin() + end);
            tx_q.push(p);
        }
        tx_ready.notify();
    }

    void broadcast(int n, int pkt_type, int ch_start,
                   const std::vector<float>& dv) {
        for (int i = 0; i < n; i++)
            enqueue(i, pkt_type, ch_start, dv);
    }

    Packet* wait_rx() {
        if (rx_q.empty()) wait(rx_arrived);
        while (rx_q.empty()) wait(rx_arrived);
        Packet* p = rx_q.front(); rx_q.pop();
        return p;
    }

    std::map<int,std::vector<float>> gather(int n_cores, int floats_per_core) {
        int nt = n_tiles(floats_per_core);
        std::map<int, std::map<int,std::vector<float>>> store;
        for (int i = 0; i < n_cores * nt; i++) {
            Packet* p = wait_rx();
            store[p->source_id][p->tile_idx] = p->datas;
            delete p;
        }
        std::map<int,std::vector<float>> res;
        for (auto it = store.begin(); it != store.end(); ++it) {
            int src = it->first;
            for (auto jt = it->second.begin(); jt != it->second.end(); ++jt)
                res[src].insert(res[src].end(),
                                jt->second.begin(), jt->second.end());
        }
        return res;
    }

    // -------------------------------------------------------
    // DMA helpers: DRAM <-> SRAM <-> vector
    //
    // dma_read / dma_write always stage through SRAM.
    // SRAM is 128 KB (32768 floats). Large transfers are
    // split into SRAM-sized chunks automatically.
    //
    // fm_store / fm_load: feature map management.
    //   If fm fits in SRAM: keep on-chip (no DRAM write).
    //   If fm > SRAM:       spill to DRAM intermediate region.
    // This models real HW behavior: small buffers stay on-chip,
    // large feature maps must be written back to DRAM.
    // -------------------------------------------------------
    std::vector<float> dma_read(unsigned int dram_addr, int n_floats) {
        int cap = sram_->capacity();
        std::vector<float> result;
        result.reserve(n_floats);
        int offset = 0;
        while (offset < n_floats) {
            int chunk = std::min(n_floats - offset, cap);
            std::vector<float> buf = dma_->read(
                dram_addr + (unsigned)(offset * 4), chunk);
            sram_->write(0, buf);
            std::vector<float> sram_out = sram_->read(0, chunk);
            result.insert(result.end(), sram_out.begin(), sram_out.end());
            offset += chunk;
        }
        return result;
    }

    void dma_write(unsigned int dram_addr, const std::vector<float>& data) {
        int n_floats = (int)data.size();
        int cap      = sram_->capacity();
        int offset   = 0;
        while (offset < n_floats) {
            int chunk = std::min(n_floats - offset, cap);
            sram_->write(0, data.data() + offset, chunk);
            std::vector<float> sram_out = sram_->read(0, chunk);
            dma_->write(dram_addr + (unsigned)(offset * 4), sram_out);
            offset += chunk;
        }
    }

    // -------------------------------------------------------
    // Feature map spill management
    // -------------------------------------------------------
    unsigned int inter_offset_ = 0;  // next free byte in DRAM_INTER_BASE

    struct FmHandle {
        unsigned int       dram_addr;  // valid if spilled to DRAM
        std::vector<float> data;       // non-empty if kept on-chip
        bool spilled() const { return data.empty(); }
        int  size()    const { return (int)data.size(); }
    };

    // Store feature map after a layer.
    // If n_floats <= SRAM capacity: keep in returned vector.
    // Otherwise: dma_write to DRAM intermediate, return empty data.
    FmHandle fm_store(const std::vector<float>& fm,
                      const std::string& name) {
        int n   = (int)fm.size();
        int cap = sram_->capacity();
        if (n <= cap) {
            LOG2("[FM] " << name << " (" << n*4/1024
                 << " KB) fits in SRAM (" << cap*4/1024
                 << " KB) -- kept on-chip");
            return {0u, fm};
        }
        // Spill to DRAM
        unsigned int addr = DRAM_INTER_BASE + inter_offset_;
        inter_offset_ += (unsigned)(n * 4);
        LOG2("[FM] " << name << " (" << n*4/1024
             << " KB) > SRAM (" << cap*4/1024
             << " KB) -- spilled to DRAM @ 0x"
             << std::hex << addr << std::dec);
        dma_write(addr, fm);
        return {addr, {}};
    }

    // Load feature map for next layer.
    // If on-chip: return directly. If spilled: dma_read from DRAM.
    std::vector<float> fm_load(const FmHandle& h, int n_floats) {
        if (!h.spilled()) {
            LOG2("[FM] loading from on-chip cache (" << n_floats*4/1024 << " KB)");
            return h.data;
        }
        LOG2("[FM] loading from DRAM @ 0x"
             << std::hex << h.dram_addr << std::dec
             << " (" << n_floats*4/1024 << " KB)");
        return dma_read(h.dram_addr, n_floats);
    }

    // -------------------------------------------------------
    // init_dram: load data folder into DRAM before sc_start.
    // This is the ONLY place that reads files directly.
    // -------------------------------------------------------
    void init_dram(const std::string& data_path,
                   const std::string& image_file) {
        auto load = [&](const std::string& fname,
                        unsigned int addr) -> long long {
            std::string path = data_path + fname;
            std::ifstream f(path.c_str());
            if (!f.is_open()) {
                std::cerr << "[DRAM init] Cannot open: " << path << std::endl;
                return 0LL;
            }
            std::vector<float> buf;
            float v;
            while (f >> v) buf.push_back(v);
            dram_->write_floats(addr, buf);
            return (long long)buf.size();
        };

        LOG1("[DRAM] Initializing DRAM from data folder...");
        load(image_file,        DRAM_IMAGE_BASE);  LOG2("[DRAM] image loaded");
        load("conv1_weight.txt",DRAM_C1W_BASE);
        load("conv1_bias.txt",  DRAM_C1B_BASE);    LOG2("[DRAM] conv1 w/b loaded");
        load("conv2_weight.txt",DRAM_C2W_BASE);
        load("conv2_bias.txt",  DRAM_C2B_BASE);    LOG2("[DRAM] conv2 w/b loaded");
        load("conv3_weight.txt",DRAM_C3W_BASE);
        load("conv3_bias.txt",  DRAM_C3B_BASE);    LOG2("[DRAM] conv3 w/b loaded");
        load("conv4_weight.txt",DRAM_C4W_BASE);
        load("conv4_bias.txt",  DRAM_C4B_BASE);    LOG2("[DRAM] conv4 w/b loaded");
        load("conv5_weight.txt",DRAM_C5W_BASE);
        load("conv5_bias.txt",  DRAM_C5B_BASE);    LOG2("[DRAM] conv5 w/b loaded");
        load("fc6_weight.txt",  DRAM_FC6W_BASE);
        load("fc6_bias.txt",    DRAM_FC6B_BASE);   LOG2("[DRAM] fc6  w/b loaded");
        load("fc7_weight.txt",  DRAM_FC7W_BASE);
        load("fc7_bias.txt",    DRAM_FC7B_BASE);   LOG2("[DRAM] fc7  w/b loaded");
        load("fc8_weight.txt",  DRAM_FC8W_BASE);
        load("fc8_bias.txt",    DRAM_FC8B_BASE);   LOG2("[DRAM] fc8  w/b loaded");

        // Reset stats so only runtime accesses are counted
        dram_->reset_stats();
        dma_->reset_stats();
        sram_->reset_stats();
        LOG1("[DRAM] Initialization complete. Runtime stats reset.");
    }

    // -------------------------------------------------------
    // Assemble helpers (identical to new HW4)
    // -------------------------------------------------------
    std::vector<float> assemble_conv(
            std::map<int,std::vector<float>>& parts,
            int n_cores, int ch_per, int H, int W) {
        int sp = H * W;
        std::vector<float> out(n_cores * ch_per * sp, 0.f);
        for (auto it = parts.begin(); it != parts.end(); ++it) {
            int ch_off = it->first * ch_per;
            for (int c = 0; c < ch_per; c++)
                for (int s = 0; s < sp; s++)
                    out[(ch_off+c)*sp+s] = it->second[c*sp+s];
        }
        return out;
    }

    std::vector<float> assemble_conv1(
            std::map<int,std::vector<float>>& parts, int n_cores = 4) {
        int sp = 27 * 27;
        std::vector<float> out(64 * sp, 0.f);
        for (auto it = parts.begin(); it != parts.end(); ++it) {
            int ch_per_ = 64 / n_cores;
            int ch_off  = it->first * ch_per_;
            for (int c = 0; c < ch_per_; c++)
                for (int s = 0; s < sp; s++)
                    out[(ch_off+c)*sp+s] = it->second[c*sp+s];
        }
        return out;
    }

    std::vector<float> assemble_fc(
            std::map<int,std::vector<float>>& parts,
            int n_cores, int np) {
        std::vector<float> out(n_cores * np, 0.f);
        for (auto it = parts.begin(); it != parts.end(); ++it) {
            int base = it->first * np;
            for (int j = 0; j < np; j++) out[base+j] = it->second[j];
        }
        return out;
    }

    // -------------------------------------------------------
    // Print results (same format as HW4)
    // -------------------------------------------------------
    void print_results(const std::vector<float>& lin,
                       const std::vector<float>& sm) {
        std::vector<std::string> names;
        std::ifstream f("./data/imagenet_classes.txt");
        std::string line;
        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            names.push_back(line);
        }
        while ((int)names.size() < 1000) names.push_back("unknown");

        std::vector<std::pair<float,int>> sc(1000);
        for (int i = 0; i < 1000; i++) sc[i] = {sm[i], i};
        std::sort(sc.begin(), sc.end(),
            [](const std::pair<float,int>& a, const std::pair<float,int>& b){
                return a.first > b.first; });

        std::cout << std::fixed << std::setprecision(2);
        std::cout << "Top 100 classes:" << std::endl;
        std::cout << "=================================================" << std::endl;
        std::cout << std::right << std::setw(5) << "idx"
                  << " | " << std::setw(8)  << "val"
                  << " | " << std::setw(11) << "possibility"
                  << " | " << "class name" << std::endl;
        std::cout << "-------------------------------------------------" << std::endl;
        for (int i = 0; i < 100; i++) {
            int idx = sc[i].second;
            std::cout << std::right << std::setw(5) << idx
                      << " | " << std::setw(8)  << lin[idx]
                      << " | " << std::setw(11) << sm[idx] * 100.0f
                      << " | " << names[idx] << std::endl;
        }
        std::cout << "=================================================" << std::endl;
    }

    // -------------------------------------------------------
    // run_conv: same logic as new HW4 but reads from DRAM via DMA
    // -------------------------------------------------------
    std::vector<float> run_conv(int layer,
                                const std::vector<float>& fm_in,
                                unsigned int w_addr, unsigned int b_addr,
                                int Cout, int Cin, int K,
                                int Hout, int Wout,
                                int n_cores,
                                const std::string& label) {
        int ch_per = Cout / n_cores;
        int kf     = Cin * K * K;
        LOG1("[Ctrl] " << label << " ("
             << n_cores << " cores, " << ch_per << " ch/core)...");

        LOG2("[Ctrl] " << label << " DMA loading weights from DRAM...");
        std::vector<float> w_all = dma_read(w_addr, Cout * kf);
        std::vector<float> b_all = dma_read(b_addr, Cout);

        for (int ci = 0; ci < n_cores; ci++) {
            int oc_start = ci * ch_per;
            std::vector<float> payload;
            payload.insert(payload.end(), fm_in.begin(), fm_in.end());
            payload.insert(payload.end(),
                           w_all.begin() + oc_start * kf,
                           w_all.begin() + (oc_start + ch_per) * kf);
            payload.insert(payload.end(),
                           b_all.begin() + oc_start,
                           b_all.begin() + oc_start + ch_per);
            enqueue(ci, PKT_CONV_IN, CONV_CS(layer, oc_start), payload);
        }
        LOG2("[Ctrl] " << label << " waiting " << n_cores << " results...");
        auto res    = gather(n_cores, ch_per * Hout * Wout);
        auto fm_out = assemble_conv(res, n_cores, ch_per, Hout, Wout);
        LOG1("[Ctrl] " << label << " done -> " << shape3(Cout, Hout, Wout));
        return fm_out;
    }

    // -------------------------------------------------------
    // send_fc_weights: DMA load from DRAM, then send via NoC PKT_FC_W
    // Waits for ack from each core (same as new HW4).
    // -------------------------------------------------------
    void send_fc_weights(int round,
                         unsigned int w_addr, unsigned int b_addr,
                         int n_cores, int Nin, int Nout) {
        LOG2("[Ctrl] FC" << round
             << " DMA loading weights from DRAM...");
        std::vector<float> w_all = dma_read(w_addr, Nout * n_cores * Nin);
        std::vector<float> b_all = dma_read(b_addr, Nout * n_cores);

        LOG1("[Ctrl] FC" << round << " weight send -> "
             << n_cores << " cores via NoC...");
        for (int ci = 0; ci < n_cores; ci++) {
            int ns = ci * Nout;
            std::vector<float> payload;
            payload.insert(payload.end(),
                           w_all.begin() + ns * Nin,
                           w_all.begin() + (ns + Nout) * Nin);
            payload.insert(payload.end(),
                           b_all.begin() + ns,
                           b_all.begin() + ns + Nout);
            enqueue(ci, PKT_FC_W, round, payload);
        }
        // Wait for ack from each core (core sends PKT_FC_W ack on receipt)
        for (int ci = 0; ci < n_cores; ci++) {
            Packet* ack = wait_rx(); delete ack;
        }
        LOG2("[Ctrl] FC" << round << " weights received by all cores");
    }

    // -------------------------------------------------------
    // Main logic thread
    // -------------------------------------------------------
    void logic_thread() {
        while (rst.read()) wait(); wait();

        long long t_start = (long long)sc_core::sc_time_stamp().value();

        // per-layer breakdown: [layer_name, start_ps, end_ps]
        struct LayerTiming {
            std::string name;
            long long   start;
            long long   end;
            int         active_cores;  // number of PEs active this layer
        };
        std::vector<LayerTiming> timings;

        // ============================================================
        // Image: DMA load from DRAM
        // ============================================================
        LOG1("[Ctrl] Reading image from DRAM via DMA...");
        std::vector<float> image = dma_read(DRAM_IMAGE_BASE, 3*224*224);
        LOG1("[Ctrl] Image: " << shape3(3,224,224)
             << " = " << image.size() << " floats");

        // ============================================================
        // Conv1: DMA load weights, distribute to CORES_CONV1 cores
        // ============================================================
        LOG1("[Ctrl] Conv1+Pool1 DMA loading weights...");
        std::vector<float> c1w = dma_read(DRAM_C1W_BASE, 3*64*11*11);
        std::vector<float> c1b = dma_read(DRAM_C1B_BASE, 64);
        int kf1 = 3 * 11 * 11;

        LOG1("[Ctrl] Conv1+Pool1 ("
             << CORES_CONV1 << " cores, "
             << 64/CORES_CONV1 << " ch/core)...");
        long long ts = (long long)sc_core::sc_time_stamp().value();
        for (int ci = 0; ci < CORES_CONV1; ci++) {
            int ocs = ci * 16;
            std::vector<float> payload;
            payload.insert(payload.end(), image.begin(), image.end());
            payload.insert(payload.end(),
                           c1w.begin() + ocs*kf1,
                           c1w.begin() + (ocs+16)*kf1);
            payload.insert(payload.end(),
                           c1b.begin() + ocs, c1b.begin() + ocs + 16);
            enqueue(ci, PKT_CONV1_IN, CONV_CS(1, ocs), payload);
        }
        auto r1     = gather(CORES_CONV1, (64/CORES_CONV1)*27*27);
        auto fm6427_raw = assemble_conv1(r1, CORES_CONV1);
        LOG1("[Ctrl] Conv1+Pool1 done -> " << shape3(64,27,27));
        timings.push_back({"Conv1+Pool1", ts, (long long)sc_core::sc_time_stamp().value(), CORES_CONV1});
        // fm6427: 64*27*27 = 46656 floats = 182 KB > 128 KB SRAM -> spill
        auto h_fm6427 = fm_store(fm6427_raw, "Conv1_out[64x27x27]");

        // ============================================================
        // Conv2-5: DMA loads weights from DRAM
        // Feature maps spill to DRAM if > SRAM capacity (128 KB)
        // ============================================================
        ts = (long long)sc_core::sc_time_stamp().value();
        auto fm6427   = fm_load(h_fm6427, 64*27*27);
        auto fm19213_raw = run_conv(2, fm6427,
            DRAM_C2W_BASE, DRAM_C2B_BASE,
            192, 64, 5, 13, 13, CORES_CONV2, "Conv2+Pool2");
        timings.push_back({"Conv2+Pool2", ts, (long long)sc_core::sc_time_stamp().value(), CORES_CONV2});
        // fm19213: 192*13*13 = 32448 floats = 127 KB <= 128 KB SRAM -> on-chip
        auto h_fm19213 = fm_store(fm19213_raw, "Conv2_out[192x13x13]");

        ts = (long long)sc_core::sc_time_stamp().value();
        auto fm19213  = fm_load(h_fm19213, 192*13*13);
        auto fm38413_raw = run_conv(3, fm19213,
            DRAM_C3W_BASE, DRAM_C3B_BASE,
            384, 192, 3, 13, 13, CORES_CONV3, "Conv3");
        timings.push_back({"Conv3",       ts, (long long)sc_core::sc_time_stamp().value(), CORES_CONV3});
        // fm38413: 384*13*13 = 64896 floats = 254 KB > 128 KB SRAM -> spill
        auto h_fm38413 = fm_store(fm38413_raw, "Conv3_out[384x13x13]");

        ts = (long long)sc_core::sc_time_stamp().value();
        auto fm38413  = fm_load(h_fm38413, 384*13*13);
        auto fm25613_raw = run_conv(4, fm38413,
            DRAM_C4W_BASE, DRAM_C4B_BASE,
            256, 384, 3, 13, 13, CORES_CONV4, "Conv4");
        timings.push_back({"Conv4",       ts, (long long)sc_core::sc_time_stamp().value(), CORES_CONV4});
        // fm25613: 256*13*13 = 43264 floats = 169 KB > 128 KB SRAM -> spill
        auto h_fm25613 = fm_store(fm25613_raw, "Conv4_out[256x13x13]");

        ts = (long long)sc_core::sc_time_stamp().value();
        auto fm25613  = fm_load(h_fm25613, 256*13*13);
        auto flat9216_raw = run_conv(5, fm25613,
            DRAM_C5W_BASE, DRAM_C5B_BASE,
            256, 256, 3, 6, 6, CORES_CONV5, "Conv5+Pool5");
        timings.push_back({"Conv5+Pool5", ts, (long long)sc_core::sc_time_stamp().value(), CORES_CONV5});
        // flat9216: 9216 floats = 36 KB <= 128 KB SRAM -> on-chip
        auto h_flat9216 = fm_store(flat9216_raw, "Conv5_out[9216]");

        // ============================================================
        // FC6: DMA load weights -> NoC PKT_FC_W -> inference
        // ============================================================
        ts = (long long)sc_core::sc_time_stamp().value();
        send_fc_weights(6, DRAM_FC6W_BASE, DRAM_FC6B_BASE,
                        CORES_FC6, 9216, 4096/CORES_FC6);

        LOG1("[Ctrl] FC6 inference (" << CORES_FC6 << " cores, WS)...");
        auto flat9216 = fm_load(h_flat9216, 9216);
        broadcast(CORES_FC6, PKT_FC_IN, 6, flat9216);
        auto rfc6   = gather(CORES_FC6, 4096/CORES_FC6);
        auto fc6out_raw = assemble_fc(rfc6, CORES_FC6, 4096/CORES_FC6);
        LOG1("[Ctrl] FC6 done -> " << shape1(4096));
        timings.push_back({"FC6", ts, (long long)sc_core::sc_time_stamp().value(), CORES_FC6});
        // fc6out: 4096 floats = 16 KB <= 128 KB SRAM -> on-chip
        auto h_fc6out = fm_store(fc6out_raw, "FC6_out[4096]");

        // ============================================================
        // FC7: DMA load weights -> NoC PKT_FC_W -> inference
        // ============================================================
        ts = (long long)sc_core::sc_time_stamp().value();
        send_fc_weights(7, DRAM_FC7W_BASE, DRAM_FC7B_BASE,
                        CORES_FC7, 4096, 4096/CORES_FC7);

        LOG1("[Ctrl] FC7 inference (" << CORES_FC7 << " cores, WS)...");
        auto fc6out = fm_load(h_fc6out, 4096);
        broadcast(CORES_FC7, PKT_FC_IN, 7, fc6out);
        auto rfc7   = gather(CORES_FC7, 4096/CORES_FC7);
        auto fc7out_raw = assemble_fc(rfc7, CORES_FC7, 4096/CORES_FC7);
        LOG1("[Ctrl] FC7 done -> " << shape1(4096));
        timings.push_back({"FC7", ts, (long long)sc_core::sc_time_stamp().value(), CORES_FC7});
        // fc7out: 4096 floats = 16 KB <= 128 KB SRAM -> on-chip
        auto h_fc7out = fm_store(fc7out_raw, "FC7_out[4096]");

        // ============================================================
        // FC8: DMA load weights -> NoC PKT_FC_W -> inference
        // ============================================================
        LOG1("[Ctrl] FC8 DMA loading weights from DRAM...");
        ts = (long long)sc_core::sc_time_stamp().value();
        {
            std::vector<float> fc8w = dma_read(DRAM_FC8W_BASE, 4096*1000);
            std::vector<float> fc8b = dma_read(DRAM_FC8B_BASE, 1000);
            std::vector<float> payload;
            payload.insert(payload.end(), fc8w.begin(), fc8w.end());
            payload.insert(payload.end(), fc8b.begin(), fc8b.end());
            enqueue(CORE_FC8, PKT_FC_W, 8, payload);
            Packet* ack = wait_rx(); delete ack;
            LOG2("[Ctrl] FC8 weights received by Core " << CORE_FC8);
        }

        LOG1("[Ctrl] FC8+Softmax @ Core " << CORE_FC8 << "...");
        auto fc7out = fm_load(h_fc7out, 4096);
        enqueue(CORE_FC8, PKT_FC8_IN, 0, fc7out);
        Packet* result = wait_rx();
        LOG1("[Ctrl] Inference done.");
        timings.push_back({"FC8+Softmax", ts, (long long)sc_core::sc_time_stamp().value(), 1});

        std::vector<float> lin(result->datas.begin(),
                                result->datas.begin() + 1000);
        std::vector<float> sm(result->datas.begin() + 1000,
                               result->datas.end());
        delete result;

        // ============================================================
        // Write output to DRAM before printing (spec requirement).
        // Layout: [0..999] = linear output (val), [1000..1999] = softmax probability
        // ============================================================
        LOG1("[Ctrl] Writing output results to DRAM [0x"
             << std::hex << DRAM_OUTPUT_BASE << std::dec << "]...");
        std::vector<float> out_buf;
        out_buf.reserve(2000);
        out_buf.insert(out_buf.end(), lin.begin(), lin.end()); // 1000 floats: linear
        out_buf.insert(out_buf.end(), sm.begin(),  sm.end());  // 1000 floats: probability
        dma_write(DRAM_OUTPUT_BASE, out_buf);

        // Read back from DRAM for printing
        std::vector<float> readback = dma_read(DRAM_OUTPUT_BASE, 2000);
        std::vector<float> lin_out(readback.begin(), readback.begin() + 1000);
        std::vector<float> sm_readback(readback.begin() + 1000, readback.end());

        // ============================================================
        // Print execution metrics
        // ============================================================
        long long t_end   = (long long)sc_core::sc_time_stamp().value();
        long long sim_ns  = (t_end - t_start) / 1000; // ps -> ns
        long long sim_cyc = sim_ns / CLK_PERIOD_NS;

        // PE utilization: weighted average across layers
        long long weighted_core_cycles = 0;
        for (auto& t : timings) {
            long long layer_ns  = (t.end - t.start) / 1000;
            long long layer_cyc = layer_ns / CLK_PERIOD_NS;
            weighted_core_cycles += layer_cyc * t.active_cores;
        }
        double pe_util = (sim_cyc > 0)
            ? (double)weighted_core_cycles / ((double)sim_cyc * 16) * 100.0
            : 0.0;

        std::cout << std::endl;
        std::cout << "===== Execution Metrics (Baseline) =============" << std::endl;
        std::cout << "  MAC per PE    : " << MAC_PER_PE << std::endl;
        std::cout << "  Total MACs    : " << MAC_PER_PE * 16 << std::endl;
        std::cout << "  On-chip SRAM  : 1 bank x 128 KB = 128 KB total" << std::endl;
        std::cout << "  Local SRAM/PE : 0 KB (none)" << std::endl;
        std::cout << "  SRAM bit width: 32 bits (1 float per access)" << std::endl;
        std::cout << "  DRAM bit width: 32 bits (1 float per AXI beat)" << std::endl;
        std::cout << "  Sim time      : " << sim_ns  << " ns" << std::endl;
        std::cout << "  Sim cycles    : " << sim_cyc << " cycles" << std::endl;
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "  PE utilization: " << pe_util
                  << "% (weighted avg = active_cores/16 x layer_time%)" << std::endl;

        // Per-layer time breakdown
        std::cout << std::endl;
        std::cout << "  --- Per-layer Time Breakdown ---" << std::endl;
        std::cout << "  " << std::left  << std::setw(14) << "Layer"
                  << std::right << std::setw(12) << "Cycles"
                  << std::setw(10) << "Time(ns)"
                  << std::setw(7)  << "%"
                  << std::setw(8)  << "Cores"
                  << std::setw(10) << "PE util" << std::endl;
        std::cout << "  " << std::string(61, '-') << std::endl;
        for (auto& t : timings) {
            long long layer_ns  = (t.end - t.start) / 1000;
            long long layer_cyc = layer_ns / CLK_PERIOD_NS;
            double    pct       = (sim_ns > 0)
                ? (double)layer_ns / sim_ns * 100.0 : 0.0;
            double layer_util = (double)t.active_cores / 16.0 * 100.0;
            std::cout << "  " << std::left  << std::setw(14) << t.name
                      << std::right << std::setw(12) << layer_cyc
                      << std::setw(10) << layer_ns
                      << std::setw(6)  << pct << "%"
                      << std::setw(8)  << t.active_cores << "/16"
                      << std::setw(8)  << layer_util << "%" << std::endl;
        }
        std::cout << std::endl;

        dram_->print_stats();
        dma_->print_stats();
        sram_->print_stats();
        std::cout << "=================================================" << std::endl;
        std::cout << std::endl;

        print_results(lin_out, sm_readback);

        sc_stop();
    }

    SC_HAS_PROCESS(Controller);
    explicit Controller(sc_module_name name) : sc_module(name) {
        dram_ = new DRAM();
        dma_  = new AXI4_DMA("axi4_dma", dram_);  // SC_MODULE ctor
        sram_ = new SRAM(32768);    // 128 KB on-chip SRAM (baseline)

        SC_THREAD(logic_thread); sensitive << clk.pos();
        SC_THREAD(tx_thread);    sensitive << clk.pos();
        SC_THREAD(rx_thread);    sensitive << clk.pos();
    }

    ~Controller() {
        delete sram_;
        delete dma_;
        delete dram_;
    }
};

#endif // CONTROLLER_H
