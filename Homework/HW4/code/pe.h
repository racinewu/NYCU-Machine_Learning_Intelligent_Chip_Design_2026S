#ifndef PE_H
#define PE_H

#include <vector>
#include <string>
#include <cstring>
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

// ==================================================
// 1. Configurable Bit Widths (Modify these to rescale the whole system layout):
//    - TYPE_WIDTH      : FLIT_HEAD=2, FLIT_TAIL=1, FLIT_BODY=0)
//    - FLOAT_WIDTH     : Fixed hardware IEEE-754 single precision float width)
//    - FLOATS_PER_FLIT : NoC packet bandwidth control; must be a power of 2)
//    - ID_WIDTH        : PE Destination and Source routing node address width)
//    - PKT_TYPE_WIDTH  : Packet Type indicator field width)
//    - CH_START_WIDTH  : Channel Start encoding/layer info width)
//    - TILE_IDX_WIDTH  : Data tiling sequence index field width)
//
// 2. Structural Packet Geometry (Total Width = TYPE_WIDTH + FLOATS_PER_FLIT * FLOAT_WIDTH):
//    - Header  : [Type] [Dest ID]     [Src ID]        [Padding...=0]
//    - Meta    : [Type] [Packet Type] [Channel Start] [Padding...=0]
//    - TileHdr : [Type] [Tile Index]                  [Padding...=0]
//    - Body    : [Type] [float0]      [float1]  ...   [floatN]
//    - Tail    : [Type] same layout as Body, unused slots zero-padded
//
// 3. Bit Range Formula Mapping (MSB-to-LSB expressions used across the codebase):
//    - Type    : [ FLIT_WIDTH - 1 : FLIT_WIDTH - TYPE_WIDTH ]
//    - Dest    : [ DATA_HI : DATA_HI - ID_WIDTH + 1 ]
//    - Src     : [ DATA_HI - ID_WIDTH : DATA_HI - 2 * ID_WIDTH + 1 ]
//    - Pkt     : [ DATA_HI : DATA_HI - PKT_TYPE_WIDTH + 1 ]
//    - Chan    : [ DATA_HI - PKT_TYPE_WIDTH : DATA_HI - PKT_TYPE_WIDTH - CH_START_WIDTH + 1 ]
//    - Tile    : [ DATA_HI : DATA_HI - TILE_IDX_WIDTH + 1 ]
//    - float[i]: [ DATA_HI - i * FLOAT_WIDTH : DATA_HI - (i + 1) * FLOAT_WIDTH + 1 ]
// ==================================================

#define TYPE_WIDTH       2
#define FLOAT_WIDTH     32
#define FLOATS_PER_FLIT  4

#define ID_WIDTH        16
#define PKT_TYPE_WIDTH   8
#define CH_START_WIDTH  16
#define TILE_IDX_WIDTH  16


#define DATA_WIDTH      (FLOATS_PER_FLIT * FLOAT_WIDTH)
#define FLIT_WIDTH      (TYPE_WIDTH + DATA_WIDTH)

// Main field boundaries: Most Significant Bits (MSB) for Type, the remaining for Data
#define TYPE_HI         (FLIT_WIDTH - 1)
#define TYPE_LO         (FLIT_WIDTH - TYPE_WIDTH)
#define DATA_HI         (TYPE_LO - 1)
#define DATA_LO         0

// Header packet fields (Left-aligned within data payload)
#define HDR_DEST_HI     DATA_HI
#define HDR_DEST_LO     (HDR_DEST_HI - ID_WIDTH + 1)
#define HDR_SRC_HI      (HDR_DEST_LO - 1)
#define HDR_SRC_LO      (HDR_SRC_HI - ID_WIDTH + 1)

// Meta packet fields (Left-aligned within data payload)
#define META_TYPE_HI    DATA_HI
#define META_TYPE_LO    (META_TYPE_HI - PKT_TYPE_WIDTH + 1)
#define META_CH_HI      (META_TYPE_LO - 1)
#define META_CH_LO      (META_CH_HI - CH_START_WIDTH + 1)

// TileHdr packet fields (Left-aligned within data payload)
#define TILE_IDX_HI     DATA_HI
#define TILE_IDX_LO     (TILE_IDX_HI - TILE_IDX_WIDTH + 1)

// Parameterized position derivation for each float in the Data packet
#define FLOAT_HI(i)     (DATA_HI - (i) * FLOAT_WIDTH)
#define FLOAT_LO(i)     (FLOAT_HI(i) - FLOAT_WIDTH + 1)

#define ID_MASK         ((1ULL << ID_WIDTH) - 1)
#define PKT_TYPE_MASK   ((1ULL << PKT_TYPE_WIDTH) - 1)
#define CH_START_MASK   ((1ULL << CH_START_WIDTH) - 1)
#define TILE_IDX_MASK   ((1ULL << TILE_IDX_WIDTH) - 1)


static_assert(sizeof(float) * 8 == FLOAT_WIDTH, "FLOAT_WIDTH must match system float size!");
static_assert(FLOAT_WIDTH == 32, "This design specifically expects 32-bit float layout.");
static_assert((FLOATS_PER_FLIT > 0) && ((FLOATS_PER_FLIT & (FLOATS_PER_FLIT - 1)) == 0), "FLOATS_PER_FLIT must be a power of 2!");
static_assert(FLOAT_HI(0) >= FLOAT_LO(0), "SystemC Range indexing layout underflow detection!");
static_assert(FLOAT_HI(FLOATS_PER_FLIT - 1) >= 0, "Lowest float field exceeds flit bounds!");

// ch_start encoding for PKT_CONV_IN: [15:12]=layer [11:0]=oc_start
#define CONV_CS(layer, oc_start) (((layer)<<12)|((oc_start)&0xFFF))


inline sc_lv<FLIT_WIDTH> make_header(int dest, int src) {
    sc_lv<FLIT_WIDTH> h = 0;
    h.range(TYPE_HI, TYPE_LO) = FLIT_HEAD;
    h.range(HDR_DEST_HI, HDR_DEST_LO) = dest & ID_MASK;
    h.range(HDR_SRC_HI, HDR_SRC_LO)   = src  & ID_MASK;
    return h;
}

inline sc_lv<FLIT_WIDTH> make_meta(int pkt_type, int ch_start) {
    sc_lv<FLIT_WIDTH> f = 0;
    f.range(TYPE_HI, TYPE_LO) = FLIT_BODY;
    f.range(META_TYPE_HI, META_TYPE_LO) = pkt_type & PKT_TYPE_MASK;
    f.range(META_CH_HI, META_CH_LO)     = ch_start & CH_START_MASK;
    return f;
}

inline sc_lv<FLIT_WIDTH> make_tile_hdr(int tile_idx) {
    sc_lv<FLIT_WIDTH> f = 0;
    f.range(TYPE_HI, TYPE_LO) = FLIT_BODY;
    f.range(TILE_IDX_HI, TILE_IDX_LO) = tile_idx & TILE_IDX_MASK;
    return f;
}

// Pack up to FLOATS_PER_FLIT floats into one data flit.
// Unused slots are zero-padded.
// tail=true sets type bits to FLIT_TAIL.
inline sc_lv<FLIT_WIDTH> make_data_flit(const float* vals, int count, bool tail) {
    sc_lv<FLIT_WIDTH> f = 0;
    f.range(TYPE_HI, TYPE_LO) = tail ? FLIT_TAIL : FLIT_BODY;
    
    for (int i = 0; i < FLOATS_PER_FLIT; i++) {
        float fv = (i < count) ? vals[i] : 0.f;
        uint32_t iv;
        std::memcpy(&iv, &fv, sizeof(float));
        f.range(FLOAT_HI(i), FLOAT_LO(i)) = iv;
    }
    return f;
}

// Extract up to FLOATS_PER_FLIT floats from a data flit into out[].
// Returns number of valid floats (always FLOATS_PER_FLIT; caller trims to actual).
inline void flit_to_floats(sc_lv<FLIT_WIDTH> f, float* out) {
    for (int i = 0; i < FLOATS_PER_FLIT; i++) {
        uint32_t iv = f.range(FLOAT_HI(i), FLOAT_LO(i)).to_uint();
        float fv;
        std::memcpy(&fv, &iv, sizeof(float));
        out[i] = fv;
    }
}

struct Packet {
    int source_id = 0, dest_id = 0;
    int pkt_type  = 0, ch_start = 0, tile_idx = 0;
    std::vector<float> datas;
};

#endif