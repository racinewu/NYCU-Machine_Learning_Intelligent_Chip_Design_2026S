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
// 4-phase handshake: send one flit
// -------------------------------------------------------
template<typename F, typename R, typename A>
inline void noc_send_flit(sc_lv<34> f, F& flit_tx, R& req_tx, A& ack_tx) {
    req_tx.write(1); flit_tx.write(f);
    while (ack_tx.read() == 0) sc_core::wait();
    req_tx.write(0);
    while (ack_tx.read() == 1) sc_core::wait();
}

// -------------------------------------------------------
// Send one packet:
//   Header flit  -> router routes by dest
//   Meta flit    -> pkt_type + ch_start
//   TileHdr flit -> tile_idx (16-bit)
//   Data flits   -> float payload
//   (if no data: meta flit is TAIL, no tilehdr/data)
// -------------------------------------------------------
template<typename F, typename R, typename A>
inline void noc_send_packet(int dest, int src,
                             int pkt_type, int ch_start, int tile_idx,
                             const std::vector<float>& datas,
                             F& flit_tx, R& req_tx, A& ack_tx) {
    noc_send_flit(make_header(dest, src), flit_tx, req_tx, ack_tx);
    if (datas.empty()) {
        sc_lv<34> m = make_meta(pkt_type, ch_start);
        m.range(33,32) = FLIT_TAIL;
        noc_send_flit(m, flit_tx, req_tx, ack_tx);
        return;
    }
    noc_send_flit(make_meta(pkt_type, ch_start),  flit_tx, req_tx, ack_tx);
    noc_send_flit(make_tile_hdr(tile_idx),         flit_tx, req_tx, ack_tx);
    for (int i = 0; i < (int)datas.size(); i++)
        noc_send_flit(make_data_flit(datas[i], i==(int)datas.size()-1),
                      flit_tx, req_tx, ack_tx);
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
// Receive one complete packet (blocks until tail flit)
// Parses 3-flit header: Header -> Meta -> TileHdr -> Data
// -------------------------------------------------------
template<typename F, typename R, typename A>
inline Packet* noc_recv_packet(F& flit_rx, R& req_rx, A& ack_rx) {
    Packet* p = nullptr;
    // State: 0=wait_head 1=wait_meta 2=wait_tilehdr 3=wait_data
    int state = 0;

    while (true) {
        while (req_rx.read() == 0) sc_core::wait();
        sc_lv<34> f = flit_rx.read();
        ack_rx.write(1);
        while (req_rx.read() == 1) sc_core::wait();
        ack_rx.write(0);

        int ftype = f.range(33,32).to_uint();

        if (ftype == FLIT_HEAD) {
            delete p;
            p = new Packet();
            p->dest_id   = f.range(31,16).to_uint();
            p->source_id = f.range(15, 0).to_uint();
            state = 1; // next: meta
        } else if (state == 1) {
            // Meta flit
            p->pkt_type = f.range(31,24).to_uint();
            p->ch_start = f.range(23, 8).to_uint();
            if (ftype == FLIT_TAIL) return p; // zero-data packet
            state = 2; // next: tile_hdr
        } else if (state == 2) {
            // TileHdr flit
            p->tile_idx = f.range(31,16).to_uint();
            state = 3; // next: data
        } else if (state == 3 && p) {
            p->datas.push_back(flit_to_float(f));
            if (ftype == FLIT_TAIL) return p;
        }
        sc_core::wait();
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