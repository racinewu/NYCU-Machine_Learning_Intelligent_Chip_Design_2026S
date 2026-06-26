#ifndef CONTROLLER_H
#define CONTROLLER_H

#include "systemc.h"
#include "noc_io.h"
#include "config.h"
#include <vector>
#include <queue>
#include <map>
#include <string>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <cmath>


SC_MODULE(Controller) {
    sc_in  <bool>       rst;
    sc_in  <bool>       clk;
    sc_out <int>        layer_id;
    sc_out <bool>       layer_id_type;
    sc_out <bool>       layer_id_valid;
    sc_in  <float>      data;
    sc_in  <bool>       data_valid;
    sc_out <sc_lv<FLIT_WIDTH>>  flit_tx;
    sc_out <bool>       req_tx;

    // FC weight cache (read from ROM, then sent to cores via NoC PKT_FC_W)
    std::vector<float> fc6_w_cache, fc6_b_cache;
    std::vector<float> fc7_w_cache, fc7_b_cache;
    std::vector<float> fc8_w_cache, fc8_b_cache;
    sc_in  <bool>       ack_tx;
    sc_in  <sc_lv<FLIT_WIDTH>>  flit_rx;
    sc_in  <bool>       req_rx;
    sc_out <bool>       ack_rx;

    std::queue<Packet*> tx_q;
    sc_event       tx_ready;
    std::queue<Packet*> rx_q;
    sc_event       rx_arrived;

    void tx_thread() {
        req_tx.write(0); flit_tx.write(0);
        while (true) {
            if (tx_q.empty()) wait(tx_ready);
            if (tx_q.empty()) continue;
            Packet* p=tx_q.front(); tx_q.pop();
            noc_send_packet(p->dest_id, CTRL_ID,
                            p->pkt_type, p->ch_start, p->tile_idx,
                            p->datas, flit_tx, req_tx, ack_tx);
            delete p;
        }
    }

    void rx_thread() {
        ack_rx.write(0);
        while (true) {
            Packet* p=noc_recv_packet(flit_rx, req_rx, ack_rx);
            if (p) { rx_q.push(p); rx_arrived.notify(); }
        }
    }

    void enqueue(int dest, int pkt_type, int ch_start,
                 const std::vector<float>& datas) {
        int total=(int)datas.size();
        if (total==0) {
            Packet* p=new Packet();
            p->dest_id=dest; p->pkt_type=pkt_type;
            p->ch_start=ch_start; p->tile_idx=0;
            tx_q.push(p); tx_ready.notify(); return;
        }
        for (int off=0,tidx=0; off<total; off+=TILE_SIZE,tidx++) {
            Packet* p=new Packet();
            p->dest_id=dest; p->pkt_type=pkt_type;
            p->ch_start=ch_start; p->tile_idx=tidx;
            int end=std::min(off+TILE_SIZE,total);
            p->datas.insert(p->datas.end(),datas.begin()+off,datas.begin()+end);
            tx_q.push(p);
        }
        tx_ready.notify();
    }

    void broadcast(int n, int pkt_type, int ch_start,
                   const std::vector<float>& dv) {
        for (int i=0;i<n;i++) enqueue(i, pkt_type, ch_start, dv);
    }

    Packet* wait_rx() {
        if (rx_q.empty()) wait(rx_arrived);
        while (rx_q.empty()) wait(rx_arrived);
        Packet* p=rx_q.front(); rx_q.pop();
        return p;
    }

    // Gather n_cores*ntiles packets, reassemble per source
    std::map<int,std::vector<float>> gather(int n_cores, int floats_per_core) {
        int nt=n_tiles(floats_per_core);
        std::map<int, std::map<int,std::vector<float>>> store;
        for (int i=0;i<n_cores*nt;i++) {
            Packet* p=wait_rx();
            store[p->source_id][p->tile_idx]=p->datas;
            delete p;
        }
        std::map<int,std::vector<float>> res;
        for (auto it=store.begin();it!=store.end();++it) {
            int src=it->first;
            for (auto jt=it->second.begin();jt!=it->second.end();++jt)
                res[src].insert(res[src].end(),jt->second.begin(),jt->second.end());
        }
        return res;
    }

    std::vector<float> rom_read(int lid, bool ltype) {
        std::vector<float> buf;
        layer_id.write(lid); layer_id_type.write(ltype);
        layer_id_valid.write(true); wait();
        layer_id_valid.write(false);
        while (!data_valid.read()) wait();
        int count = 0;
        // Read all floats; log interval calculated after total is known.
        // We use a two-stage approach: buffer silently, then print summary.
        // For large layers, print every rom_log_interval(estimated_total) floats.
        // Estimate: use layer sizes known at compile time, keyed by lid.
        // lid: 1=conv1w, 2=conv2w, 3=conv3w, 4=conv4w, 5=conv5w, 6=fc6w, 7=fc7w, 8=fc8w
        // bias sizes are small, interval will be large (no prints)
        static const int known_total[] = {
            0,                    // 0: image (handled elsewhere)
            3*64*11*11,           // 1: conv1 weight = 23232
            64*192*5*5,           // 2: conv2 weight = 307200
            192*384*3*3,          // 3: conv3 weight = 663552
            384*256*3*3,          // 4: conv4 weight = 884736
            256*256*3*3,          // 5: conv5 weight = 589824
            9216*4096,            // 6: fc6 weight = 37748736
            4096*4096,            // 7: fc7 weight = 16777216
            4096*1000             // 8: fc8 weight = 4096000
        };
        int est_total = (lid >= 0 && lid <= 8) ? known_total[lid] : 0;
        if (ltype) est_total = 0; // bias is tiny, skip interval logging
        int interval = (est_total > 0) ? rom_log_interval(est_total) : 0;

        while (data_valid.read()) {
            buf.push_back(data.read());
            count++;
            if (interval > 0 && count % interval == 0) {
                // Format count as xK or xM
                std::ostringstream unit;
                if (count >= 1000000)      unit << count/1000000 << "M";
                else if (count >= 1000)    unit << count/1000    << "K";
                else                       unit << count;
                LOG2("[ROM] Layer " << lid << (ltype?" bias":" weight")
                    << " read " << std::setw(5) << unit.str() << " floats...");
            }
            wait();
        }
        wait();
        LOG2("[ROM] Layer " << lid << (ltype?" bias":" weight")
            << " done: " << count << " floats");
        return buf;
    }

    // Assemble conv output: each core returned ch_per channels
    std::vector<float> assemble_conv(std::map<int,std::vector<float>>& parts,
                                 int n_cores, int ch_per, int H, int W) {
        int sp=H*W;
        std::vector<float> out(n_cores*ch_per*sp,0.f);
        for (auto it=parts.begin();it!=parts.end();++it) {
            int src=it->first;
            int ch_off=src*ch_per;
            for (int c=0;c<ch_per;c++)
                for (int s=0;s<sp;s++)
                    out[(ch_off+c)*sp+s]=it->second[c*sp+s];
        }
        return out;
    }

    // Assemble conv1: 4 cores, 16 ch each
    std::vector<float> assemble_conv1(std::map<int,std::vector<float>>& parts, int n_cores=4) {
        int sp=27*27;
        std::vector<float> out(64*sp,0.f);
        for (auto it=parts.begin();it!=parts.end();++it) {
            int ch_per_=64/n_cores; int ch_off=it->first*ch_per_;
            for (int c=0;c<ch_per_;c++)
                for (int s=0;s<sp;s++)
                    out[(ch_off+c)*sp+s]=it->second[c*sp+s];
        }
        return out;
    }

    std::vector<float> assemble_fc(std::map<int,std::vector<float>>& parts,
                               int n_cores, int np) {
        std::vector<float> out(n_cores*np,0.f);
        for (auto it=parts.begin();it!=parts.end();++it) {
            int base=it->first*np;
            for (int j=0;j<np;j++) out[base+j]=it->second[j];
        }
        return out;
    }

    void print_results(const std::vector<float>& lin, const std::vector<float>& sm) {
        std::vector<std::string> names;
        ifstream f("./data/imagenet_classes.txt");
        std::string line;
        while (getline(f,line)) {
            if (!line.empty()&&line.back()=='\r') line.pop_back();
            names.push_back(line);
        }
        while ((int)names.size()<1000) names.push_back("unknown");
        std::vector<std::pair<float,int>> sc(1000);
        for (int i=0;i<1000;i++) sc[i]={sm[i],i};
        sort(sc.begin(),sc.end(),[](const std::pair<float,int>&a,const std::pair<float,int>&b){
            return a.first>b.first;});
        std::cout<<std::fixed<<std::setprecision(2);
        std::cout<<"Top 100 classes:"<<endl;
        std::cout<<"================================================="<<endl;
        std::cout<<std::right<<std::setw(5)<<"idx"<<" | "<<std::setw(8)<<"val"
            <<" | "<<std::setw(11)<<"possibility"<<" | "<<"class name"<<endl;
        std::cout<<"-------------------------------------------------"<<endl;
        for (int i=0;i<100;i++) {
            int idx=sc[i].second;
            std::cout<<std::right<<std::setw(5)<<idx<<" | "<<std::setw(8)<<lin[idx]
                <<" | "<<std::setw(11)<<sm[idx]*100.0f<<" | "<<names[idx]<<endl;
        }
        std::cout<<"================================================="<<endl;
    }

    // Run one conv layer: pack [FM | weights | biases] per core, 1 round
    // Each core gets ch_per output channels worth of weights
    std::vector<float> run_conv(int layer,
                            const std::vector<float>& fm_in,
                            int rom_lid,
                            int Cout, int Cin, int K,
                            int Hout, int Wout,
                            int n_cores,
                            const std::string& label) {
        int ch_per = Cout / n_cores;
        int kf     = Cin*K*K;
        LOG1("[Ctrl] " << label << " (" << n_cores << " cores, " << ch_per << " ch/core)...");

        std::vector<float> w_all = rom_read(rom_lid, false);
        std::vector<float> b_all = rom_read(rom_lid, true);

        for (int ci=0; ci<n_cores; ci++) {
            int oc_start = ci*ch_per;
            std::vector<float> payload;
            payload.insert(payload.end(), fm_in.begin(), fm_in.end());
            payload.insert(payload.end(),
                           w_all.begin()+oc_start*kf,
                           w_all.begin()+(oc_start+ch_per)*kf);
            payload.insert(payload.end(),
                           b_all.begin()+oc_start,
                           b_all.begin()+oc_start+ch_per);
            enqueue(ci, PKT_CONV_IN, CONV_CS(layer, oc_start), payload);
        }
        LOG2("[Ctrl] " << label << " waiting " << n_cores << " results...");
        auto res = gather(n_cores, ch_per*Hout*Wout);
        auto fm_out = assemble_conv(res, n_cores, ch_per, Hout, Wout);
        LOG1("[Ctrl] " << label << " done -> " << shape3(Cout,Hout,Wout));
        return fm_out;
    }

    void logic_thread() {
        layer_id_valid.write(false);
        layer_id.write(0); layer_id_type.write(false);
        while (rst.read()) wait(); wait();

        // Read image
        LOG1("[Ctrl] Reading image...");
        std::vector<float> image = rom_read(0, false);
        LOG1("[Ctrl] Image: " << shape3(3,224,224) << " = " << image.size() << " floats");

        LOG1("[Ctrl] Conv1 reading weights from ROM...");
        std::vector<float> c1w = rom_read(1, false);
        std::vector<float> c1b = rom_read(1, true);
        int kf1 = 3*11*11;

        LOG1("[Ctrl] Conv1+Pool1 (" << CORES_CONV1 << " cores, " << 64/CORES_CONV1 << " ch/core)...");
        for (int ci=0; ci<CORES_CONV1; ci++) {
            int ocs=ci*16;
            std::vector<float> payload;
            payload.insert(payload.end(), image.begin(), image.end());
            payload.insert(payload.end(), c1w.begin()+ocs*kf1, c1w.begin()+(ocs+16)*kf1);
            payload.insert(payload.end(), c1b.begin()+ocs, c1b.begin()+ocs+16);
            enqueue(ci, PKT_CONV1_IN, CONV_CS(1, ocs), payload);
        }
        auto r1 = gather(CORES_CONV1, (64/CORES_CONV1)*27*27);
        auto fm6427 = assemble_conv1(r1, CORES_CONV1);
        LOG1("[Ctrl] Conv1+Pool1 done -> " << shape3(64,27,27));

        // Conv2-5 (1 round each, all 16 cores)
        auto fm19213 = run_conv(2, fm6427,  2, 192,  64, 5, 13, 13, CORES_CONV2, "Conv2+Pool2");
        auto fm38413 = run_conv(3, fm19213, 3, 384, 192, 3, 13, 13, CORES_CONV3, "Conv3");
        auto fm25613 = run_conv(4, fm38413, 4, 256, 384, 3, 13, 13, CORES_CONV4, "Conv4");
        auto flat9216= run_conv(5, fm25613, 5, 256, 256, 3,  6,  6, CORES_CONV5, "Conv5+Pool5");

        // ==================================================
        // FC weight preload: read from ROM then direct-inject into cores
        // Done sequentially after Conv5 to avoid ROM signal contention.
        // wait(cycles) advances simulation time by theoretical NoC TX time.
        // ==================================================
        LOG1("[Ctrl] FC6 reading weights from ROM...");
        fc6_w_cache = rom_read(6, false);
        fc6_b_cache = rom_read(6, true);
        LOG2("[Ctrl] FC6 weight read: " << fc6_w_cache.size() << " floats");

        LOG1("[Ctrl] FC6 weight send -> " << CORES_FC6 << " cores via NoC...");
        {
            const int Nin=9216, Nout=4096/CORES_FC6;
            for (int ci=0; ci<CORES_FC6; ci++) {
                int ns = ci * Nout;
                std::vector<float> payload;
                payload.insert(payload.end(),
                               fc6_w_cache.begin() + ns*Nin,
                               fc6_w_cache.begin() + (ns+Nout)*Nin);
                payload.insert(payload.end(),
                               fc6_b_cache.begin() + ns,
                               fc6_b_cache.begin() + ns + Nout);
                enqueue(ci, PKT_FC_W, 6, payload);
            }
            for (int ci=0; ci<CORES_FC6; ci++) {
                Packet* ack = wait_rx(); delete ack;
            }
            LOG2("[Ctrl] FC6 weights received by all cores");
        }

        // FC6 inference
        LOG1("[Ctrl] FC6 inference (" << CORES_FC6 << " cores, WS)...");
        broadcast(CORES_FC6, PKT_FC_IN, 6, flat9216);
        auto rfc6 = gather(CORES_FC6, 4096/CORES_FC6);
        auto fc6out = assemble_fc(rfc6, CORES_FC6, 4096/CORES_FC6);
        LOG1("[Ctrl] FC6 done -> " << shape1(4096));

        LOG1("[Ctrl] FC7 reading weights from ROM...");
        fc7_w_cache = rom_read(7, false);
        fc7_b_cache = rom_read(7, true);
        LOG2("[Ctrl] FC7 weight read: " << fc7_w_cache.size() << " floats");

        LOG1("[Ctrl] FC7 weight send -> " << CORES_FC7 << " cores via NoC...");
        {
            const int Nin=4096, Nout=4096/CORES_FC7;
            for (int ci=0; ci<CORES_FC7; ci++) {
                int ns = ci * Nout;
                std::vector<float> payload;
                payload.insert(payload.end(),
                               fc7_w_cache.begin() + ns*Nin,
                               fc7_w_cache.begin() + (ns+Nout)*Nin);
                payload.insert(payload.end(),
                               fc7_b_cache.begin() + ns,
                               fc7_b_cache.begin() + ns + Nout);
                enqueue(ci, PKT_FC_W, 7, payload);
            }
            for (int ci=0; ci<CORES_FC7; ci++) {
                Packet* ack = wait_rx(); delete ack;
            }
            LOG2("[Ctrl] FC7 weights received by all cores");
        }

        // FC7 inference
        LOG1("[Ctrl] FC7 inference (" << CORES_FC7 << " cores, WS)...");
        broadcast(CORES_FC7, PKT_FC_IN, 7, fc6out);
        auto rfc7 = gather(CORES_FC7, 4096/CORES_FC7);
        auto fc7out = assemble_fc(rfc7, CORES_FC7, 4096/CORES_FC7);
        LOG1("[Ctrl] FC7 done -> " << shape1(4096));

        LOG1("[Ctrl] FC8 reading weights from ROM...");
        fc8_w_cache = rom_read(8, false);
        fc8_b_cache = rom_read(8, true);

        // FC8+Softmax: send weights to Core CORE_FC8 via NoC
        LOG1("[Ctrl] FC8+Softmax @ Core " << CORE_FC8 << "...");
        {
            std::vector<float> payload;
            payload.insert(payload.end(), fc8_w_cache.begin(), fc8_w_cache.end());
            payload.insert(payload.end(), fc8_b_cache.begin(), fc8_b_cache.end());
            enqueue(CORE_FC8, PKT_FC_W, 8, payload);
            Packet* ack = wait_rx(); delete ack;
            LOG2("[Ctrl] FC8 weights received by Core " << CORE_FC8);
        }
        enqueue(CORE_FC8, PKT_FC8_IN, 0, fc7out);
        Packet* result = wait_rx();
        LOG1("[Ctrl] Done.");

        std::vector<float> lin(result->datas.begin(), result->datas.begin()+1000);
        std::vector<float> sm(result->datas.begin()+1000, result->datas.end());
        delete result;
        print_results(lin, sm);

        sc_stop();
    }

    SC_CTOR(Controller) {
        SC_THREAD(logic_thread);    sensitive << clk.pos();
        SC_THREAD(tx_thread);       sensitive << clk.pos();
        SC_THREAD(rx_thread);       sensitive << clk.pos();
    }
};

#endif