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
// Flit layout (wide flit: 4 floats per data flit):
//
//   Header  : [129:128]=10  [127:112]=dest  [111:96]=src  [95:0]=0
//   Meta    : [129:128]=00  [127:120]=pkt_type  [119:104]=ch_start  [103:0]=0
//   TileHdr : [129:128]=00  [127:112]=tile_idx  [111:0]=0
//   Body    : [129:128]=00  [127:96]=float0  [95:64]=float1
//                           [63:32]=float2   [31:0]=float3
//   Tail    : [129:128]=01  same layout, unused slots zero-padded
//
// FLIT_WIDTH = 130 bits = 2 (type) + 4*32 (floats)
// FLOATS_PER_FLIT = 4
// ============================================================

#define FLIT_WIDTH      130
#define FLOATS_PER_FLIT 4

inline sc_lv<FLIT_WIDTH> make_header(int dest, int src) {
    sc_lv<FLIT_WIDTH> h = 0;
    h.range(129,128) = FLIT_HEAD;
    h.range(127,112) = dest & 0xFFFF;
    h.range(111, 96) = src  & 0xFFFF;
    return h;
}

inline sc_lv<FLIT_WIDTH> make_meta(int pkt_type, int ch_start) {
    sc_lv<FLIT_WIDTH> f = 0;
    f.range(129,128) = FLIT_BODY;
    f.range(127,120) = pkt_type & 0xFF;
    f.range(119,104) = ch_start & 0xFFFF;
    return f;
}

inline sc_lv<FLIT_WIDTH> make_tile_hdr(int tile_idx) {
    sc_lv<FLIT_WIDTH> f = 0;
    f.range(129,128) = FLIT_BODY;
    f.range(127,112) = tile_idx & 0xFFFF;
    return f;
}

// Pack up to FLOATS_PER_FLIT floats into one data flit.
// vals[0] -> [127:96], vals[1] -> [95:64], vals[2] -> [63:32], vals[3] -> [31:0]
// Unused slots are zero-padded.
// tail=true sets type bits to FLIT_TAIL.
inline sc_lv<FLIT_WIDTH> make_data_flit(const float* vals, int count, bool tail) {
    sc_lv<FLIT_WIDTH> f = 0;
    f.range(129,128) = tail ? FLIT_TAIL : FLIT_BODY;
    union { float fv; unsigned iv; } cv;
    for (int i = 0; i < FLOATS_PER_FLIT; i++) {
        cv.fv = (i < count) ? vals[i] : 0.f;
        int hi = 127 - i*32;
        int lo = hi - 31;
        f.range(hi, lo) = cv.iv;
    }
    return f;
}

// Extract up to FLOATS_PER_FLIT floats from a data flit into out[].
// Returns number of valid floats (always FLOATS_PER_FLIT; caller trims to actual).
inline void flit_to_floats(sc_lv<FLIT_WIDTH> f, float* out) {
    union { float fv; unsigned iv; } cv;
    for (int i = 0; i < FLOATS_PER_FLIT; i++) {
        int hi = 127 - i*32;
        int lo = hi - 31;
        cv.iv = f.range(hi, lo).to_uint();
        out[i] = cv.fv;
    }
}

struct Packet {
    int source_id = 0, dest_id = 0;
    int pkt_type  = 0, ch_start = 0, tile_idx = 0;
    std::vector<float> datas;
    long long send_time_ps = 0;  // sc_time_stamp().value() when header flit sent
};

#endif