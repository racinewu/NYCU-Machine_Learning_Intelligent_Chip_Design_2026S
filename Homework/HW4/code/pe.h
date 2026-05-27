#ifndef PE_H
#define PE_H

#include <vector>
#include <string>
#include "systemc.h"
#include "config.h"

#define FLIT_BODY  0
#define FLIT_TAIL  1
#define FLIT_HEAD  2

#define PKT_CONV1_IN  0
#define PKT_CONV_IN   1
#define PKT_FC_W      3
#define PKT_FC_IN     4
#define PKT_FC8_IN    5
#define PKT_CONV_OUT  6
#define PKT_FC_OUT    7
#define PKT_RESULT    8

#define TILE_SIZE 32768
#define CTRL_ID   16

// ch_start encoding for PKT_CONV_IN: [15:12]=layer [11:0]=oc_start
#define CONV_CS(layer, oc_start) (((layer)<<12)|((oc_start)&0xFFF))

// ============================================================
// Flit layout:
//   Header  : [33:32]=10  [31:16]=dest  [15:0]=src
//   Meta    : [33:32]=00  [31:24]=pkt_type  [23:8]=ch_start  [7:0]=0
//   TileHdr : [33:32]=00  [31:16]=tile_idx  [15:0]=0
//   Body    : [33:32]=00  [31:0]=float bits
//   Tail    : [33:32]=01  [31:0]=float bits
// ============================================================


inline sc_lv<34> make_header(int dest, int src) {
    sc_lv<34> h = 0;
    h.range(33,32) = FLIT_HEAD;
    h.range(31,16) = dest & 0xFFFF;
    h.range(15, 0) = src  & 0xFFFF;
    return h;
}
inline sc_lv<34> make_meta(int pkt_type, int ch_start) {
    sc_lv<34> f = 0;
    f.range(33,32) = FLIT_BODY;
    f.range(31,24) = pkt_type & 0xFF;
    f.range(23, 8) = ch_start & 0xFFFF;
    return f;
}
inline sc_lv<34> make_tile_hdr(int tile_idx) {
    sc_lv<34> f = 0;
    f.range(33,32) = FLIT_BODY;
    f.range(31,16) = tile_idx & 0xFFFF;
    return f;
}
inline sc_lv<34> make_data_flit(float v, bool tail) {
    sc_lv<34> f = 0;
    f.range(33,32) = tail ? FLIT_TAIL : FLIT_BODY;
    union { float fv; unsigned iv; } cv; cv.fv = v;
    f.range(31,0) = cv.iv;
    return f;
}
inline float flit_to_float(sc_lv<34> f) {
    union { float fv; unsigned iv; } cv;
    cv.iv = f.range(31,0).to_uint();
    return cv.fv;
}

struct Packet {
    int source_id = 0, dest_id = 0;
    int pkt_type  = 0, ch_start = 0, tile_idx = 0;
    std::vector<float> datas;
};

#endif