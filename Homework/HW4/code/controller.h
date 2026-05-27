#ifndef CONTROLLER_H
#define CONTROLLER_H

#include "systemc.h"
#include "noc_io.h"
#include <vector>
#include <queue>
#include <map>
#include <string>
#include <algorithm>
#include <fstream>
#include <iomanip>
using namespace std;

// ch_start for PKT_CONV_IN:
//   [15:12] = layer (1-5)
//   [11: 0] = global output channel start for this core (core_id * ch_per_core)
#define CONV_CS(layer, ch_start) (((layer)<<12)|((ch_start)&0xFFF))

SC_MODULE(Controller) {
    sc_in  <bool>       rst;
    sc_in  <bool>       clk;
    sc_out <int>        layer_id;
    sc_out <bool>       layer_id_type;
    sc_out <bool>       layer_id_valid;
    sc_in  <float>      data;
    sc_in  <bool>       data_valid;
    sc_out <sc_lv<34>>  flit_tx;
    sc_out <bool>       req_tx;
    sc_in  <bool>       ack_tx;
    sc_in  <sc_lv<34>>  flit_rx;
    sc_in  <bool>       req_rx;
    sc_out <bool>       ack_rx;

    queue<Packet*> tx_q;
    sc_event       tx_ready;
    queue<Packet*> rx_q;
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
                 const vector<float>& datas) {
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
            int end=min(off+TILE_SIZE,total);
            p->datas.insert(p->datas.end(),datas.begin()+off,datas.begin()+end);
            tx_q.push(p);
        }
        tx_ready.notify();
    }

    void broadcast(int n, int pkt_type, int ch_start,
                   const vector<float>& dv) {
        for (int i=0;i<n;i++) enqueue(i, pkt_type, ch_start, dv);
    }

    Packet* wait_rx() {
        if (rx_q.empty()) wait(rx_arrived);
        while (rx_q.empty()) wait(rx_arrived);
        Packet* p=rx_q.front(); rx_q.pop();
        return p;
    }

    // Gather n_cores*ntiles packets, reassemble per source
    map<int,vector<float>> gather(int n_cores, int floats_per_core) {
        int nt=n_tiles(floats_per_core);
        map<int, map<int,vector<float>>> store;
        for (int i=0;i<n_cores*nt;i++) {
            Packet* p=wait_rx();
            store[p->source_id][p->tile_idx]=p->datas;
            delete p;
        }
        map<int,vector<float>> res;
        for (auto it=store.begin();it!=store.end();++it) {
            int src=it->first;
            for (auto jt=it->second.begin();jt!=it->second.end();++jt)
                res[src].insert(res[src].end(),jt->second.begin(),jt->second.end());
        }
        return res;
    }

    vector<float> rom_read(int lid, bool ltype) {
        vector<float> buf;
        layer_id.write(lid); layer_id_type.write(ltype);
        layer_id_valid.write(true); wait();
        layer_id_valid.write(false);
        while (!data_valid.read()) wait();
        int count = 0;
        while (data_valid.read()) {
            buf.push_back(data.read());
            count++;
            if (count % 10000 == 0)
                cout<<"[ROM] layer "<<lid<<(ltype?" bias":" weight")
                    <<" read "<<count/10000<<"0K floats..."<<endl;
            wait();
        }
        wait();
        return buf;
    }

    // Assemble conv output: each core returned ch_per channels
    vector<float> assemble_conv(map<int,vector<float>>& parts,
                                 int n_cores, int ch_per, int H, int W) {
        int sp=H*W;
        vector<float> out(n_cores*ch_per*sp,0.f);
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
    vector<float> assemble_conv1(map<int,vector<float>>& parts) {
        int sp=27*27;
        vector<float> out(64*sp,0.f);
        for (auto it=parts.begin();it!=parts.end();++it) {
            int ch_off=it->first*16;
            for (int c=0;c<16;c++)
                for (int s=0;s<sp;s++)
                    out[(ch_off+c)*sp+s]=it->second[c*sp+s];
        }
        return out;
    }

    vector<float> assemble_fc(map<int,vector<float>>& parts,
                               int n_cores, int np) {
        vector<float> out(n_cores*np,0.f);
        for (auto it=parts.begin();it!=parts.end();++it) {
            int base=it->first*np;
            for (int j=0;j<np;j++) out[base+j]=it->second[j];
        }
        return out;
    }

    void print_results(const vector<float>& lin, const vector<float>& sm) {
        vector<string> names;
        ifstream f("./data/imagenet_classes.txt");
        string line;
        while (getline(f,line)) {
            if (!line.empty()&&line.back()=='\r') line.pop_back();
            names.push_back(line);
        }
        while ((int)names.size()<1000) names.push_back("unknown");
        vector<pair<float,int>> sc(1000);
        for (int i=0;i<1000;i++) sc[i]={sm[i],i};
        sort(sc.begin(),sc.end(),[](const pair<float,int>&a,const pair<float,int>&b){
            return a.first>b.first;});
        cout<<fixed<<setprecision(2);
        cout<<"Top 100 classes:"<<endl;
        cout<<"================================================="<<endl;
        cout<<right<<setw(5)<<"idx"<<" | "<<setw(8)<<"val"
            <<" | "<<setw(11)<<"possibility"<<" | "<<"class name"<<endl;
        cout<<"-------------------------------------------------"<<endl;
        for (int i=0;i<100;i++) {
            int idx=sc[i].second;
            cout<<right<<setw(5)<<idx<<" | "<<setw(8)<<lin[idx]
                <<" | "<<setw(11)<<sm[idx]*100.0f<<" | "<<names[idx]<<endl;
        }
        cout<<"================================================="<<endl;
    }

    // Run one conv layer: pack [FM | weights | biases] per core, 1 round
    // Each core gets ch_per output channels worth of weights
    vector<float> run_conv(int layer,
                            const vector<float>& fm_in,
                            int rom_lid,
                            int Cout, int Cin, int K,
                            int Hout, int Wout,
                            const string& label) {
        int ch_per = Cout / 16;
        int kf     = Cin*K*K;
        cout<<"[Ctrl] "<<label<<" (16 cores, "<<ch_per<<" ch/core)..."<<endl;

        vector<float> w_all = rom_read(rom_lid, false);
        vector<float> b_all = rom_read(rom_lid, true);

        // Send packed payload to each core: [FM | weight_slice | bias_slice]
        for (int ci=0; ci<16; ci++) {
            int oc_start = ci*ch_per;
            vector<float> payload;
            payload.insert(payload.end(), fm_in.begin(), fm_in.end());
            payload.insert(payload.end(),
                           w_all.begin()+oc_start*kf,
                           w_all.begin()+(oc_start+ch_per)*kf);
            payload.insert(payload.end(),
                           b_all.begin()+oc_start,
                           b_all.begin()+oc_start+ch_per);
            enqueue(ci, PKT_CONV_IN, CONV_CS(layer, oc_start), payload);
        }
        cout<<"[Ctrl] "<<label<<" waiting 16 results..."<<endl;
        auto res = gather(16, ch_per*Hout*Wout);
        auto fm_out = assemble_conv(res, 16, ch_per, Hout, Wout);
        cout<<"[Ctrl] "<<label<<" done -> "<<fm_out.size()<<" floats"<<endl;
        return fm_out;
    }

    void logic_thread() {
        layer_id_valid.write(false);
        layer_id.write(0); layer_id_type.write(false);
        while (rst.read()) wait(); wait();

        // ---- Conv1 ----
        cout<<"[Ctrl] Reading image..."<<endl;
        vector<float> image = rom_read(0, false);
        cout<<"[Ctrl] Image: "<<image.size()<<" floats"<<endl;

        cout<<"[Ctrl] Reading Conv1 weights..."<<endl;
        vector<float> c1w = rom_read(1, false);
        vector<float> c1b = rom_read(1, true);
        int kf1 = 3*11*11;

        cout<<"[Ctrl] Conv1+Pool1 (4 cores, 16 ch/core)..."<<endl;
        for (int ci=0; ci<4; ci++) {
            int ocs=ci*16;
            vector<float> payload;
            payload.insert(payload.end(), image.begin(), image.end());
            payload.insert(payload.end(), c1w.begin()+ocs*kf1, c1w.begin()+(ocs+16)*kf1);
            payload.insert(payload.end(), c1b.begin()+ocs, c1b.begin()+ocs+16);
            enqueue(ci, PKT_CONV1_IN, CONV_CS(1, ocs), payload);
        }
        auto r1 = gather(4, 16*27*27);
        auto fm6427 = assemble_conv1(r1);
        cout<<"[Ctrl] Conv1+Pool1 done -> "<<fm6427.size()<<" floats"<<endl;

        // ---- Conv2-5 (1 round each, all 16 cores) ----
        auto fm19213 = run_conv(2, fm6427,  2, 192,  64, 5, 13, 13, "Conv2+Pool2");
        auto fm38413 = run_conv(3, fm19213, 3, 384, 192, 3, 13, 13, "Conv3");
        auto fm25613 = run_conv(4, fm38413, 4, 256, 384, 3, 13, 13, "Conv4");
        auto flat9216= run_conv(5, fm25613, 5, 256, 256, 3,  6,  6, "Conv5+Pool5");

        // ---- FC weight preload (WS: send once, reuse) ----
        cout<<"[Ctrl] Preloading FC6 weights -> 8 cores..."<<endl;
        {
            cout<<"[Ctrl] Reading FC6 weight from ROM..."<<endl;
            vector<float> w=rom_read(6,false);
            cout<<"[Ctrl] FC6 weight read: "<<w.size()<<" floats"<<endl;
            cout<<"[Ctrl] Reading FC6 bias from ROM..."<<endl;
            vector<float> b=rom_read(6,true);
            cout<<"[Ctrl] FC6 bias read: "<<b.size()<<" floats, dispatching to 8 cores..."<<endl;
            for (int ci=0;ci<8;ci++) {
                int ns=ci*512; int Nin=9216;
                vector<float> pl;
                pl.insert(pl.end(), w.begin()+ns*Nin, w.begin()+(ns+512)*Nin);
                pl.insert(pl.end(), b.begin()+ns,     b.begin()+ns+512);
                enqueue(ci, PKT_FC_W, 6, pl);
                cout<<"[Ctrl] FC6 weight dispatched to Core "<<ci<<endl;
            }
        }
        cout<<"[Ctrl] Preloading FC7 weights -> 8 cores..."<<endl;
        {
            cout<<"[Ctrl] Reading FC7 weight from ROM..."<<endl;
            vector<float> w=rom_read(7,false);
            cout<<"[Ctrl] FC7 weight read: "<<w.size()<<" floats"<<endl;
            cout<<"[Ctrl] Reading FC7 bias from ROM..."<<endl;
            vector<float> b=rom_read(7,true);
            cout<<"[Ctrl] FC7 bias read: "<<b.size()<<" floats, dispatching..."<<endl;
            for (int ci=0;ci<8;ci++) {
                int ns=ci*512; int Nin=4096;
                vector<float> pl;
                pl.insert(pl.end(), w.begin()+ns*Nin, w.begin()+(ns+512)*Nin);
                pl.insert(pl.end(), b.begin()+ns,     b.begin()+ns+512);
                enqueue(ci, PKT_FC_W, 7, pl);
                cout<<"[Ctrl] FC7 weight dispatched to Core "<<ci<<endl;
            }
        }
        cout<<"[Ctrl] Preloading FC8 weights -> Core 15..."<<endl;
        {
            cout<<"[Ctrl] Reading FC8 weight from ROM..."<<endl;
            vector<float> w=rom_read(8,false);
            cout<<"[Ctrl] FC8 weight read: "<<w.size()<<" floats"<<endl;
            vector<float> b=rom_read(8,true);
            cout<<"[Ctrl] FC8 bias read: "<<b.size()<<" floats, dispatching..."<<endl;
            vector<float> pl;
            pl.insert(pl.end(),w.begin(),w.end());
            pl.insert(pl.end(),b.begin(),b.end());
            enqueue(15, PKT_FC_W, 8, pl);
            cout<<"[Ctrl] FC8 weight dispatched to Core 15"<<endl;
        }

        // ---- FC6 ----
        cout<<"[Ctrl] FC6 (8 cores, WS)..."<<endl;
        broadcast(8, PKT_FC_IN, 6, flat9216);
        auto r6=gather(8,512);
        auto fc6out=assemble_fc(r6,8,512);
        cout<<"[Ctrl] FC6 done -> "<<fc6out.size()<<" floats"<<endl;

        // ---- FC7 ----
        cout<<"[Ctrl] FC7 (8 cores, WS)..."<<endl;
        broadcast(8, PKT_FC_IN, 7, fc6out);
        auto r7=gather(8,512);
        auto fc7out=assemble_fc(r7,8,512);
        cout<<"[Ctrl] FC7 done -> "<<fc7out.size()<<" floats"<<endl;

        // ---- FC8+Softmax ----
        cout<<"[Ctrl] FC8+Softmax @ Core 15..."<<endl;
        enqueue(15, PKT_FC8_IN, 0, fc7out);
        Packet* result=wait_rx();
        cout<<"[Ctrl] Done."<<endl;

        vector<float> lin(result->datas.begin(),result->datas.begin()+1000);
        vector<float> sm(result->datas.begin()+1000,result->datas.end());
        delete result;
        print_results(lin,sm);
        sc_stop();
    }

    SC_CTOR(Controller) {
        SC_THREAD(logic_thread); sensitive << clk.pos();
        SC_THREAD(tx_thread);    sensitive << clk.pos();
        SC_THREAD(rx_thread);    sensitive << clk.pos();
    }
};

#endif