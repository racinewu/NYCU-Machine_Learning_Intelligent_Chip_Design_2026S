#ifndef CORE_H
#define CORE_H

#include "systemc.h"
#include "noc_io.h"
#include "config.h"
#include <vector>
#include <queue>
#include <map>
#include <string>
#include <cmath>
#include <algorithm>
using namespace std;

// ============================================================
// Core assignment (Controller at Router 0, Cores 1-15 at Routers 1-15):
//
//   Core 1-4  : Conv1 (16 out-ch each) + Conv2-5 + FC6 + FC7
//   Core 5-8  :                          Conv2-5 + FC6 + FC7
//   Core 9    : FC8 + Softmax
//   Core 10-15: idle (all ports stubbed in main.cpp)
//
// Conv2-5 uses 8 cores (1-8), each handles Cout/8 channels:
//   Conv2: 192/8 = 24 ch/core
//   Conv3: 384/8 = 48 ch/core
//   Conv4: 256/8 = 32 ch/core
//   Conv5: 256/8 = 32 ch/core
//
// FC6-7 uses 8 cores (1-8), Weight Stationary:
//   512 neurons per core, weights preloaded via NoC from ROM
//
// All weights arrive via NoC from Controller (which reads ROM).
// No direct file I/O in Core.
// ============================================================

SC_MODULE(Core) {
    sc_in  <bool>       rst;
    sc_in  <bool>       clk;
    sc_in  <sc_lv<34>>  flit_rx;
    sc_in  <bool>       req_rx;
    sc_out <bool>       ack_rx;
    sc_out <sc_lv<34>>  flit_tx;
    sc_out <bool>       req_tx;
    sc_in  <bool>       ack_tx;

    int    core_id;
    string data_dir;

    // FC weight buffers loaded via PKT_FC_W packets from Controller
    // FC6/FC7: all 16 cores, 256 neurons each
    // FC8: core 15 only, 1000 neurons
    vector<float> fc6_w, fc6_b;
    vector<float> fc7_w, fc7_b;
    vector<float> fc8_w, fc8_b;  // core 15 only

    // NI TX queue (NI -> Router)
    queue<Packet*> tx_queue;
    sc_event       tx_ready;

    // NI -> PE queue: fully reassembled logical packets
    // rx_thread (NI) assembles tiles then pushes here;
    // compute_thread (PE) pops and performs NN computation.
    struct ComputeJob {
        int pkt_type;
        int ch_start;   // layer+oc encoding or round number
        vector<float> payload;
    };
    queue<ComputeJob> compute_queue;
    sc_event          compute_ready;

    void init(int id, const string& dir = "") {
        core_id  = id;
        data_dir = dir;
        // All weights arrive via NoC from Controller (which reads ROM).
        // No direct file I/O here.
    }

    // ============================================================
    // PE compute functions (no file I/O)
    // ============================================================

    // Conv1: receive [image(3*224*224) | weight(16*3*11*11) | bias(16)]
    // Output: [16][27][27] after pool1
    void do_conv1(const vector<float>& payload, vector<float>& out) {
        int img_sz = 3*224*224, w_sz = 16*3*11*11;
        const float* img = &payload[0];
        const float* W   = &payload[img_sz];
        const float* B   = &payload[img_sz + w_sz];

        int Cin=3, Cout=16, K=11, stride=4, Hin=227, Win=227;
        int Hout=55, Wout=55;

        // Build padded 227x227 from 224x224
        vector<vector<vector<float>>> pad(Cin,
            vector<vector<float>>(Hin, vector<float>(Win, 0.f)));
        for (int c=0;c<Cin;c++)
            for (int r=0;r<224;r++)
                for (int col=0;col<224;col++)
                    pad[c][r+2][col+2] = img[c*224*224+r*224+col];

        // Conv + ReLU
        vector<vector<vector<float>>> cv(Cout,
            vector<vector<float>>(Hout, vector<float>(Wout, 0.f)));
        int k2=K*K;
        for (int oc=0;oc<Cout;oc++)
            for (int i=0;i<Hout;i++)
                for (int j=0;j<Wout;j++) {
                    float s=B[oc];
                    for (int ic=0;ic<Cin;ic++) {
                        int wb=(oc*Cin+ic)*k2;
                        for (int kh=0;kh<K;kh++)
                            for (int kw=0;kw<K;kw++)
                                s+=pad[ic][i*stride+kh][j*stride+kw]*W[wb+kh*K+kw];
                    }
                    cv[oc][i][j]=(s>0.f)?s:0.f;
                }

        // MaxPool 3x3 stride 2: 55->27
        out.clear(); out.reserve(Cout*27*27);
        for (int oc=0;oc<Cout;oc++)
            for (int i=0;i<27;i++)
                for (int j=0;j<27;j++) {
                    float mx=-1e30f;
                    for (int kh=0;kh<3;kh++)
                        for (int kw=0;kw<3;kw++)
                            mx=max(mx, cv[oc][i*2+kh][j*2+kw]);
                    out.push_back(mx);
                }
    }

    // Conv2-5: output-channel stationary
    // payload = [FM_flat | weight(n_ch*Cin*K*K) | bias(n_ch)]
    // n_ch = number of output channels this core handles
    void do_conv(int layer, const vector<float>& payload,
                 int n_ch, vector<float>& out) {
        int Cin, K, Hin, Win, pad, stride;
        if      (layer==2){ Cin=64;  K=5; Hin=27; Win=27; pad=2; stride=1; }
        else if (layer==3){ Cin=192; K=3; Hin=13; Win=13; pad=1; stride=1; }
        else if (layer==4){ Cin=384; K=3; Hin=13; Win=13; pad=1; stride=1; }
        else               { Cin=256; K=3; Hin=13; Win=13; pad=1; stride=1; }

        int fm_sz = Cin*Hin*Win;
        int kf    = Cin*K*K;
        const float* fm_raw = &payload[0];
        const float* W      = &payload[fm_sz];
        const float* B      = &payload[fm_sz + n_ch*kf];

        int Hout=(Hin+2*pad-K)/stride+1;
        int Wout=(Win+2*pad-K)/stride+1;
        int k2=K*K;

        // Build padded input
        int ph=Hin+2*pad, pw=Win+2*pad;
        vector<vector<vector<float>>> padded(Cin,
            vector<vector<float>>(ph, vector<float>(pw,0.f)));
        for (int c=0;c<Cin;c++)
            for (int r=0;r<Hin;r++)
                for (int col=0;col<Win;col++)
                    padded[c][r+pad][col+pad] = fm_raw[c*Hin*Win+r*Win+col];

        // Conv + ReLU for n_ch output channels
        vector<vector<vector<float>>> cv(n_ch,
            vector<vector<float>>(Hout, vector<float>(Wout,0.f)));
        for (int oc=0;oc<n_ch;oc++)
            for (int i=0;i<Hout;i++)
                for (int j=0;j<Wout;j++) {
                    float s=B[oc];
                    for (int ic=0;ic<Cin;ic++) {
                        int wb=(oc*Cin+ic)*k2;
                        for (int kh=0;kh<K;kh++)
                            for (int kw=0;kw<K;kw++)
                                s+=padded[ic][i*stride+kh][j*stride+kw]*W[wb+kh*K+kw];
                    }
                    cv[oc][i][j]=(s>0.f)?s:0.f;
                }

        // MaxPool if layer 2 (27->13) or 5 (13->6)
        if (layer==2) {
            out.clear(); out.reserve(n_ch*13*13);
            for (int oc=0;oc<n_ch;oc++)
                for (int i=0;i<13;i++)
                    for (int j=0;j<13;j++) {
                        float mx=-1e30f;
                        for (int kh=0;kh<3;kh++)
                            for (int kw=0;kw<3;kw++)
                                mx=max(mx,cv[oc][i*2+kh][j*2+kw]);
                        out.push_back(mx);
                    }
        } else if (layer==5) {
            out.clear(); out.reserve(n_ch*6*6);
            for (int oc=0;oc<n_ch;oc++)
                for (int i=0;i<6;i++)
                    for (int j=0;j<6;j++) {
                        float mx=-1e30f;
                        for (int kh=0;kh<3;kh++)
                            for (int kw=0;kw<3;kw++)
                                mx=max(mx,cv[oc][i*2+kh][j*2+kw]);
                        out.push_back(mx);
                    }
        } else {
            // layer 3,4: no pool
            out.clear(); out.reserve(n_ch*13*13);
            for (int oc=0;oc<n_ch;oc++)
                for (int i=0;i<Hout;i++)
                    for (int j=0;j<Wout;j++)
                        out.push_back(cv[oc][i][j]);
        }
    }

    // Compute ONE output channel given separate FM and weight+bias vectors.
    // W = [Cin*K*K floats], bias = 1 float (last element of wb was stripped by caller)
    vector<float> do_conv_one_ch(int layer,
                                  const vector<float>& fm,
                                  const vector<float>& W,
                                  float bias) {
        int Cin, K, Hin, Win, pad, stride;
        if      (layer==2){ Cin=64;  K=5; Hin=27; Win=27; pad=2; stride=1; }
        else if (layer==3){ Cin=192; K=3; Hin=13; Win=13; pad=1; stride=1; }
        else if (layer==4){ Cin=384; K=3; Hin=13; Win=13; pad=1; stride=1; }
        else               { Cin=256; K=3; Hin=13; Win=13; pad=1; stride=1; }

        int Hout=(Hin+2*pad-K)/stride+1;
        int Wout=(Win+2*pad-K)/stride+1;
        int k2=K*K;

        int ph=Hin+2*pad, pw=Win+2*pad;
        vector<vector<vector<float>>> padded(Cin,
            vector<vector<float>>(ph,vector<float>(pw,0.f)));
        for (int c=0;c<Cin;c++)
            for (int r=0;r<Hin;r++)
                for (int col=0;col<Win;col++)
                    padded[c][r+pad][col+pad]=fm[c*Hin*Win+r*Win+col];

        vector<float> out_ch(Hout*Wout);
        for (int i=0;i<Hout;i++)
            for (int j=0;j<Wout;j++) {
                float s=bias;
                for (int ic=0;ic<Cin;ic++) {
                    const float* Wic=&W[ic*k2];
                    for (int kh=0;kh<K;kh++)
                        for (int kw=0;kw<K;kw++)
                            s+=padded[ic][i*stride+kh][j*stride+kw]*Wic[kh*K+kw];
                }
                out_ch[i*Wout+j]=(s>0.f)?s:0.f;
            }

        // Pool if needed
        if (layer==2) {
            vector<float> p(13*13);
            for (int i=0;i<13;i++) for (int j=0;j<13;j++) {
                float mx=-1e30f;
                for (int kh=0;kh<3;kh++) for (int kw=0;kw<3;kw++)
                    mx=max(mx,out_ch[(i*2+kh)*Wout+(j*2+kw)]);
                p[i*13+j]=mx;
            }
            return p;
        } else if (layer==5) {
            vector<float> p(6*6);
            for (int i=0;i<6;i++) for (int j=0;j<6;j++) {
                float mx=-1e30f;
                for (int kh=0;kh<3;kh++) for (int kw=0;kw<3;kw++)
                    mx=max(mx,out_ch[(i*2+kh)*Wout+(j*2+kw)]);
                p[i*6+j]=mx;
            }
            return p;
        }
        return out_ch; // layer 3,4: 13*13

    }

    vector<float> do_fc(const vector<float>& w, const vector<float>& b,
                        const vector<float>& in, int Nout, bool relu) {
        int Nin=in.size();
        vector<float> out(Nout);
        for (int o=0;o<Nout;o++) {
            float s=b[o];
            for (int i=0;i<Nin;i++) s+=in[i]*w[o*Nin+i];
            out[o]=(relu&&s<0.f)?0.f:s;
        }
        return out;
    }

    vector<float> do_softmax(const vector<float>& in) {
        int n=in.size();
        vector<float> out(n);
        float mx=*max_element(in.begin(),in.end());
        float sm=0.f;
        for (int i=0;i<n;i++){ out[i]=exp(in[i]-mx); sm+=out[i]; }
        for (int i=0;i<n;i++) out[i]/=sm;
        return out;
    }

    // ============================================================
    // NI: enqueue outgoing tiles
    // ============================================================
    void ni_send(int dest, int pkt_type, int ch_start,
                 const vector<float>& datas) {
        int total=(int)datas.size();
        if (total==0) {
            Packet* p=new Packet();
            p->dest_id=dest; p->pkt_type=pkt_type;
            p->ch_start=ch_start; p->tile_idx=0;
            tx_queue.push(p); tx_ready.notify(); return;
        }
        for (int off=0,tidx=0; off<total; off+=TILE_SIZE,tidx++) {
            Packet* p=new Packet();
            p->dest_id=dest; p->pkt_type=pkt_type;
            p->ch_start=ch_start; p->tile_idx=tidx;
            int end=min(off+TILE_SIZE,total);
            p->datas.insert(p->datas.end(),datas.begin()+off,datas.begin()+end);
            tx_queue.push(p);
        }
        tx_ready.notify();
    }

    // ============================================================
    // TX thread
    // ============================================================
    void tx_thread() {
        req_tx.write(0); flit_tx.write(0);
        while (true) {
            if (tx_queue.empty()) wait(tx_ready);
            if (tx_queue.empty()) continue;
            Packet* p=tx_queue.front(); tx_queue.pop();
            noc_send_packet(p->dest_id, core_id,
                            p->pkt_type, p->ch_start, p->tile_idx,
                            p->datas, flit_tx, req_tx, ack_tx);
            delete p;
        }
    }

    // ============================================================
    // NI RX thread: receive flits, assemble tiles, push to compute_queue.
    // No computation here — pure protocol handling.
    // ============================================================
    void rx_thread() {
        ack_rx.write(0);
        map<int, map<int,vector<float>>> bufs;

        while (true) {
            Packet* p = noc_recv_packet(flit_rx, req_rx, ack_rx);
            if (!p) { wait(); continue; }
            LOG3("[Core " << core_id << "] recv pkt type=" << p->pkt_type
                << " ch_start=" << p->ch_start << " tile=" << p->tile_idx
                << " size=" << p->datas.size());

            int ptype = p->pkt_type;
            int cs    = p->ch_start;
            int tidx  = p->tile_idx;
            int key   = (ptype << 16) | (cs & 0xFFFF);

            bufs[key][tidx] = move(p->datas);
            delete p;

            // Determine expected tile count for this transfer
            int exp_tiles = expected_tiles(ptype, cs);
            if (exp_tiles > 0 && (int)bufs[key].size() == exp_tiles) {
                ComputeJob job;
                job.pkt_type = ptype;
                job.ch_start = cs;
                job.payload  = merge_tiles(bufs[key]);
                bufs.erase(key);
                compute_queue.push(job);
                compute_ready.notify();
            }
        }
    }

    // Returns expected number of tiles for a given (pkt_type, ch_start).
    // Returns 0 if not yet determinable (will retry next tile).
    int expected_tiles(int ptype, int cs) {
        if (ptype == PKT_CONV1_IN)
            return n_tiles(3*224*224 + 16*3*11*11 + 16);
        if (ptype == PKT_CONV_IN) {
            int layer = (cs>>12)&0xF;
            int ch_per_arr[] = {0,0,12,24,16,16};
            int Cin_arr[]    = {0,0,64,192,384,256};
            int K_arr[]      = {0,0, 5,  3,  3,  3};
            int Hin_arr[]    = {0,0,27, 13, 13, 13};
            int Win_arr[]    = {0,0,27, 13, 13, 13};
            int ch_per=ch_per_arr[layer], Cin=Cin_arr[layer], K=K_arr[layer];
            int Hin=Hin_arr[layer], Win=Win_arr[layer];
            return n_tiles(Cin*Hin*Win + ch_per*Cin*K*K + ch_per);
        }
        if (ptype == PKT_FC_W) {
            int round = cs;
            int Nin  = (round==6)?9216:4096;
            int Nout = (round==8)?1000:(4096/CORES_FC6);
            return n_tiles(Nin*Nout + Nout);
        }
        if (ptype == PKT_FC_IN) {
            int round = cs;
            return n_tiles((round==6)?9216:4096);
        }
        if (ptype == PKT_FC8_IN)
            return n_tiles(4096);
        return 0;
    }

    // ============================================================
    // PE compute thread: pop from compute_queue, run NN computation,
    // push results to tx_queue. No flit handling here.
    // ============================================================
    void compute_thread() {
        while (true) {
            if (compute_queue.empty()) wait(compute_ready);
            if (compute_queue.empty()) continue;
            ComputeJob job = compute_queue.front();
            compute_queue.pop();

            int ptype = job.pkt_type;
            int cs    = job.ch_start;
            vector<float>& flat = job.payload;

            // --- Conv1 ---
            if (ptype == PKT_CONV1_IN && core_id >= 0 && core_id <= 3) {
                vector<float> out;
                do_conv1(flat, out);
                int oc_start = cs & 0xFFF;
                LOG2("[Core " << setw(2) << core_id
                    << "] Conv1+Pool1 ch " << setw(3) << oc_start
                    << "-" << setw(3) << oc_start+15 << " done");
                ni_send(CTRL_ID, PKT_CONV_OUT, oc_start, out);

            // --- Conv2-5 ---
            } else if (ptype == PKT_CONV_IN) {
                int layer    = (cs>>12)&0xF;
                int oc_start = cs&0xFFF;
                int ch_per_arr[]={0,0,12,24,16,16};
                int ch_per = ch_per_arr[layer];
                vector<float> out;
                do_conv(layer, flat, ch_per, out);
                LOG2("[Core " << setw(2) << core_id
                    << "] Conv" << layer
                    << " ch " << setw(3) << oc_start
                    << "-" << setw(3) << oc_start+ch_per-1 << " done");
                ni_send(CTRL_ID, PKT_CONV_OUT, oc_start, out);

            // --- FC weight preload ---
            } else if (ptype == PKT_FC_W) {
                int round = cs;
                int Nin   = (round==6)?9216:4096;
                int Nout  = (round==8)?1000:(4096/CORES_FC6);
                int wsz   = Nin*Nout;
                if (round==6) {
                    fc6_w.assign(flat.begin(), flat.begin()+wsz);
                    fc6_b.assign(flat.begin()+wsz, flat.end());
                    LOG2("[Core " << core_id << "] FC6 weights loaded (" << wsz << " floats)");
                } else if (round==7) {
                    fc7_w.assign(flat.begin(), flat.begin()+wsz);
                    fc7_b.assign(flat.begin()+wsz, flat.end());
                    LOG2("[Core " << core_id << "] FC7 weights loaded (" << wsz << " floats)");
                } else if (round==8 && core_id==15) {
                    fc8_w.assign(flat.begin(), flat.begin()+wsz);
                    fc8_b.assign(flat.begin()+wsz, flat.end());
                    LOG2("[Core 15] FC8 weights loaded (" << wsz << " floats)");
                }

            // --- FC6-7 inference ---
            } else if (ptype == PKT_FC_IN && core_id >= 0 && core_id <= (CORES_FC6-1)) {
                int round = cs;
                int Nout  = 4096/CORES_FC6;
                vector<float> out;
                if (round==6) {
                    out = do_fc(fc6_w, fc6_b, flat, Nout, true);
                    LOG2("[Core " << setw(2) << core_id
                        << "] FC6 neurons " << setw(4) << core_id*Nout
                        << "-" << setw(4) << core_id*Nout+Nout-1 << " done");
                } else {
                    out = do_fc(fc7_w, fc7_b, flat, Nout, true);
                    LOG2("[Core " << setw(2) << core_id
                        << "] FC7 neurons " << setw(4) << core_id*Nout
                        << "-" << setw(4) << core_id*Nout+Nout-1 << " done");
                }
                ni_send(CTRL_ID, PKT_FC_OUT, core_id*Nout, out);

            // --- FC8 + Softmax ---
            } else if (ptype == PKT_FC8_IN && core_id == 15) {
                auto fc8_out = do_fc(fc8_w, fc8_b, flat, 1000, false);
                auto sm_out  = do_softmax(fc8_out);
                LOG2("[Core 15] FC8+Softmax done, sending result...");
                vector<float> result;
                result.reserve(2000);
                for (int i=0;i<1000;i++) result.push_back(fc8_out[i]);
                for (int i=0;i<1000;i++) result.push_back(sm_out[i]);
                ni_send(CTRL_ID, PKT_RESULT, 0, result);
            }
        }
    }


    static vector<float> merge_tiles(map<int,vector<float>>& tiles) {
        vector<float> out;
        for (auto it=tiles.begin(); it!=tiles.end(); ++it)
            out.insert(out.end(), it->second.begin(), it->second.end());
        return out;
    }

    SC_HAS_PROCESS(Core);
    Core(sc_module_name name) : sc_module(name), core_id(0) {
        SC_THREAD(tx_thread);      sensitive << clk.pos();
        SC_THREAD(rx_thread);      sensitive << clk.pos();
        SC_THREAD(compute_thread); sensitive << clk.pos();
    }
}; // end SC_MODULE(Core)

#endif