#ifndef NOC_IO_H
#define NOC_IO_H

#include "pe.h"
#include "systemc.h"
#include <vector>
#include <map>
#include <algorithm>

inline int n_tiles(int floats) {
    return (floats + TILE_SIZE - 1) / TILE_SIZE;
}

// -------------------------------------------------------
// 4-phase handshake: send one flit (wide version)
// Optimized: no trailing wait() after ack deasserts.
// -------------------------------------------------------
template<typename F, typename R, typename A>
inline void noc_send_flit(sc_lv<FLIT_WIDTH> f, F& flit_tx, R& req_tx, A& ack_tx) {
    req_tx.write(1); flit_tx.write(f);
    while (ack_tx.read() == 0) sc_core::wait();
    req_tx.write(0);
    while (ack_tx.read() == 1) sc_core::wait();
}

// -------------------------------------------------------
// Send one packet.
// Data flits now carry FLOATS_PER_FLIT floats each.
// -------------------------------------------------------
template<typename F, typename R, typename A>
inline void noc_send_packet(int dest, int src,
                             int pkt_type, int ch_start, int tile_idx,
                             const std::vector<float>& datas,
                             F& flit_tx, R& req_tx, A& ack_tx,
                             long long send_time_ps = 0) {
    noc_send_flit(make_header(dest, src), flit_tx, req_tx, ack_tx);
    if (datas.empty()) {
        sc_lv<FLIT_WIDTH> m = make_meta(pkt_type, ch_start);
        m.range(129,128) = FLIT_TAIL;
        noc_send_flit(m, flit_tx, req_tx, ack_tx);
        return;
    }
    noc_send_flit(make_meta(pkt_type, ch_start),  flit_tx, req_tx, ack_tx);
    noc_send_flit(make_tile_hdr(tile_idx),         flit_tx, req_tx, ack_tx);

    int total = (int)datas.size();
    // Send FLOATS_PER_FLIT floats per flit
    for (int off = 0; off < total; off += FLOATS_PER_FLIT) {
        int count = std::min(FLOATS_PER_FLIT, total - off);
        bool tail = (off + FLOATS_PER_FLIT >= total);
        noc_send_flit(make_data_flit(&datas[off], count, tail),
                      flit_tx, req_tx, ack_tx);
    }
}

// -------------------------------------------------------
// Send large vector in tiles of TILE_SIZE
// -------------------------------------------------------
template<typename F, typename R, typename A>
inline void noc_send_tiled(int dest, int src, int pkt_type, int ch_start,
                            const std::vector<float>& datas,
                            F& flit_tx, R& req_tx, A& ack_tx) {
    int total = (int)datas.size();
    for (int off = 0, tidx = 0; off < total; off += TILE_SIZE, tidx++) {
        int end = std::min(off + TILE_SIZE, total);
        std::vector<float> tile(datas.begin()+off, datas.begin()+end);
        noc_send_packet(dest, src, pkt_type, ch_start, tidx,
                        tile, flit_tx, req_tx, ack_tx);
    }
}

// -------------------------------------------------------
// Receive one complete packet (blocks until tail flit).
// Extracts FLOATS_PER_FLIT floats from each data flit.
// Optimized: no trailing wait() at loop bottom.
// -------------------------------------------------------
template<typename F, typename R, typename A>
inline Packet* noc_recv_packet(F& flit_rx, R& req_rx, A& ack_rx) {
    Packet* p = nullptr;
    int state = 0;

    while (true) {
        while (req_rx.read() == 0) sc_core::wait();
        sc_lv<FLIT_WIDTH> f = flit_rx.read();
        ack_rx.write(1);
        while (req_rx.read() == 1) sc_core::wait();
        ack_rx.write(0);

        int ftype = f.range(129,128).to_uint();

        if (ftype == FLIT_HEAD) {
            delete p;
            p = new Packet();
            p->dest_id   = f.range(127,112).to_uint();
            p->source_id = f.range(111, 96).to_uint();
            p->send_time_ps = (long long)sc_core::sc_time_stamp().value();
            state = 1;
        } else if (state == 1) {
            p->pkt_type = f.range(127,120).to_uint();
            p->ch_start = f.range(119,104).to_uint();
            if (ftype == FLIT_TAIL) return p;
            state = 2;
        } else if (state == 2) {
            p->tile_idx = f.range(127,112).to_uint();
            state = 3;
        } else if (state == 3 && p) {
            // Extract up to FLOATS_PER_FLIT floats
            float tmp[FLOATS_PER_FLIT];
            flit_to_floats(f, tmp);
            for (int i = 0; i < FLOATS_PER_FLIT; i++)
                p->datas.push_back(tmp[i]);
            if (ftype == FLIT_TAIL) return p;
        }
        // No trailing wait() -- loop back immediately
    }
}

// Merge sorted tile map into flat vector
inline std::vector<float> merge_tiles(std::map<int,std::vector<float>>& tiles) {
    std::vector<float> out;
    for (auto it = tiles.begin(); it != tiles.end(); ++it)
        out.insert(out.end(), it->second.begin(), it->second.end());
    return out;
}

#endif