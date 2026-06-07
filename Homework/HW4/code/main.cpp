#include "clockreset.h"
#include "pe.h"
#include "core.h"
#include "router.h"
#include "controller.h"
#include "ROM.h"
#include "systemc.h"
#include <sstream>

// 4x4 Mesh NoC:
//
//  [Ctrl]          <- connected to R0 North port (port 0)
//    |
//  R0  R1  R2  R3
//  R4  R5  R6  R7
//  R8  R9  R10 R11
//  R12 R13 R14 R15
//
// Controller ID = 16 (virtual node above R0)
// Core i at Router i LOCAL (port 4), i = 0..15
// All 16 Cores available for computation.
//
// Port map: 0=North 1=South 2=East 3=West 4=Local

sc_signal<sc_lv<FLIT_WIDTH>> sf[80];
sc_signal<bool>      sb[320];
int _sf=0, _sb=0;

void stub_port(Router* r, int p) {
    r->out_flit[p](sf[_sf]); r->in_flit[p](sf[_sf]); _sf++;
    r->out_req[p](sb[_sb++]);
    r->in_req[p] (sb[_sb++]);
    r->in_ack[p] (sb[_sb++]);
    r->out_ack[p](sb[_sb++]);
}

int sc_main(int argc, char* argv[])
{
    sc_signal<bool> clk, rst;
    Clock m_clock("m_clock", 10);
    Reset m_reset("m_reset", 15);
    m_clock(clk); m_reset(rst);

    sc_signal<int>   layer_id;
    sc_signal<bool>  layer_id_type, layer_id_valid;
    sc_signal<float> rom_data;
    sc_signal<bool>  rom_data_valid;
    ROM m_rom("m_rom");
    m_rom.clk(clk); m_rom.rst(rst);
    m_rom.layer_id(layer_id); m_rom.layer_id_type(layer_id_type);
    m_rom.layer_id_valid(layer_id_valid);
    m_rom.data(rom_data); m_rom.data_valid(rom_data_valid);

    // 16 Routers
    Router* R[16];
    for (int i=0; i<16; i++) {
        std::ostringstream s; s << "router_" << i;
        R[i] = new Router(s.str().c_str());
        R[i]->init(i); R[i]->clk(clk); R[i]->rst(rst);
    }

    // 16 Compute Cores (Core 0-15, each at Router i LOCAL)
    Core* C[16];
    for (int i=0; i<16; i++) {
        std::ostringstream s; s << "core_" << i;
        C[i] = new Core(s.str().c_str());
        C[i]->init(i, "./data");
        C[i]->clk(clk); C[i]->rst(rst);
    }

    // Controller (ID=16, connected to R0 North port)
    Controller ctrl("controller");
    ctrl.clk(clk); ctrl.rst(rst);
    ctrl.layer_id(layer_id); ctrl.layer_id_type(layer_id_type);
    ctrl.layer_id_valid(layer_id_valid);
    ctrl.data(rom_data); ctrl.data_valid(rom_data_valid);

    // -------------------------------------------------------
    // Inter-router East/West links
    // -------------------------------------------------------
    sc_signal<sc_lv<FLIT_WIDTH>> r01f,r10f, r12f,r21f, r23f,r32f;
    sc_signal<bool> r01q,r01a, r10q,r10a, r12q,r12a, r21q,r21a, r23q,r23a, r32q,r32a;
    sc_signal<sc_lv<FLIT_WIDTH>> r45f,r54f, r56f,r65f, r67f,r76f;
    sc_signal<bool> r45q,r45a, r54q,r54a, r56q,r56a, r65q,r65a, r67q,r67a, r76q,r76a;
    sc_signal<sc_lv<FLIT_WIDTH>> r89f,r98f, r9af,ra9f, rabf,rbaf;
    sc_signal<bool> r89q,r89a, r98q,r98a, r9aq,r9aa, ra9q,ra9a, rabq,raba, rbaq,rbaa;
    sc_signal<sc_lv<FLIT_WIDTH>> rcdf,rdcf, rdef,redf, reff,rfef;
    sc_signal<bool> rcdq,rcda, rdcq,rdca, rdeq,rdea, redq,reda, refq,refa, rfeq,rfea;

    // Inter-router North/South links
    sc_signal<sc_lv<FLIT_WIDTH>> r04f,r40f, r48f,r84f, r8cf,rc8f;
    sc_signal<bool> r04q,r04a, r40q,r40a, r48q,r48a, r84q,r84a, r8cq,r8ca, rc8q,rc8a;
    sc_signal<sc_lv<FLIT_WIDTH>> r15f,r51f, r59f,r95f, r9df,rd9f;
    sc_signal<bool> r15q,r15a, r51q,r51a, r59q,r59a, r95q,r95a, r9dq,r9da, rd9q,rd9a;
    sc_signal<sc_lv<FLIT_WIDTH>> r26f,r62f, r6af,ra6f, raef,reaF;
    sc_signal<bool> r26q,r26a, r62q,r62a, r6aq,r6aa, ra6q,ra6a, raeq,raea, reaq,reaA;
    sc_signal<sc_lv<FLIT_WIDTH>> r37f,r73f, r7bf,rb7f, rbff,rfbf;
    sc_signal<bool> r37q,r37a, r73q,r73a, r7bq,r7ba, rb7q,rb7a, rbfq,rbfa, rfbq,rfba;

    // Controller <-> R0 North port
    sc_signal<sc_lv<FLIT_WIDTH>> ctf, crf;
    sc_signal<bool>      ctq, cta, crq, cra;

    // Core LOCAL signals
    sc_signal<sc_lv<FLIT_WIDTH>> lf[16], lrf[16];
    sc_signal<bool>      lq[16], la[16], lrq[16], lra[16];

#define WIRE_EW(A,B, ABf,BAf, ABq,ABa, BAq,BAa) \
    R[A]->out_flit[2](ABf); R[A]->out_req[2](ABq); R[A]->in_ack[2](ABa); \
    R[A]->in_flit[2](BAf);  R[A]->in_req[2](BAq);  R[A]->out_ack[2](BAa); \
    R[B]->out_flit[3](BAf); R[B]->out_req[3](BAq); R[B]->in_ack[3](BAa); \
    R[B]->in_flit[3](ABf);  R[B]->in_req[3](ABq);  R[B]->out_ack[3](ABa);

#define WIRE_NS(A,B, ABf,BAf, ABq,ABa, BAq,BAa) \
    R[A]->out_flit[1](ABf); R[A]->out_req[1](ABq); R[A]->in_ack[1](ABa); \
    R[A]->in_flit[1](BAf);  R[A]->in_req[1](BAq);  R[A]->out_ack[1](BAa); \
    R[B]->out_flit[0](BAf); R[B]->out_req[0](BAq); R[B]->in_ack[0](BAa); \
    R[B]->in_flit[0](ABf);  R[B]->in_req[0](ABq);  R[B]->out_ack[0](ABa);

    // Row links
    WIRE_EW(0,1,  r01f,r10f, r01q,r01a, r10q,r10a)
    WIRE_EW(1,2,  r12f,r21f, r12q,r12a, r21q,r21a)
    WIRE_EW(2,3,  r23f,r32f, r23q,r23a, r32q,r32a)
    WIRE_EW(4,5,  r45f,r54f, r45q,r45a, r54q,r54a)
    WIRE_EW(5,6,  r56f,r65f, r56q,r56a, r65q,r65a)
    WIRE_EW(6,7,  r67f,r76f, r67q,r67a, r76q,r76a)
    WIRE_EW(8,9,  r89f,r98f, r89q,r89a, r98q,r98a)
    WIRE_EW(9,10, r9af,ra9f, r9aq,r9aa, ra9q,ra9a)
    WIRE_EW(10,11,rabf,rbaf, rabq,raba, rbaq,rbaa)
    WIRE_EW(12,13,rcdf,rdcf, rcdq,rcda, rdcq,rdca)
    WIRE_EW(13,14,rdef,redf, rdeq,rdea, redq,reda)
    WIRE_EW(14,15,reff,rfef, refq,refa, rfeq,rfea)

    // Column links
    WIRE_NS(0,4,  r04f,r40f, r04q,r04a, r40q,r40a)
    WIRE_NS(4,8,  r48f,r84f, r48q,r48a, r84q,r84a)
    WIRE_NS(8,12, r8cf,rc8f, r8cq,r8ca, rc8q,rc8a)
    WIRE_NS(1,5,  r15f,r51f, r15q,r15a, r51q,r51a)
    WIRE_NS(5,9,  r59f,r95f, r59q,r59a, r95q,r95a)
    WIRE_NS(9,13, r9df,rd9f, r9dq,r9da, rd9q,rd9a)
    WIRE_NS(2,6,  r26f,r62f, r26q,r26a, r62q,r62a)
    WIRE_NS(6,10, r6af,ra6f, r6aq,r6aa, ra6q,ra6a)
    WIRE_NS(10,14,raef,reaF, raeq,raea, reaq,reaA)
    WIRE_NS(3,7,  r37f,r73f, r37q,r37a, r73q,r73a)
    WIRE_NS(7,11, r7bf,rb7f, r7bq,r7ba, rb7q,rb7a)
    WIRE_NS(11,15,rbff,rfbf, rbfq,rbfa, rfbq,rfba)

    // Boundary stubs (East of col3, West of col0, South of row3)
    // R0 North is used by Controller, so NOT stubbed
    stub_port(R[0],  3); stub_port(R[4],  3);
    stub_port(R[8],  3); stub_port(R[12], 3);
    stub_port(R[3],  2); stub_port(R[7],  2);
    stub_port(R[11], 2); stub_port(R[15], 2);
    // Row 0 North: R0 used by Controller, R1-R3 stubbed
    stub_port(R[1],  0); stub_port(R[2],  0); stub_port(R[3],  0);
    // Row 3 South: all stubbed
    stub_port(R[12], 1); stub_port(R[13], 1);
    stub_port(R[14], 1); stub_port(R[15], 1);

    // Controller <-> R0 North port (port 0)
    // Controller TX -> R0 in_flit[0], Controller RX <- R0 out_flit[0]
    R[0]->in_flit[0] (ctf);  R[0]->in_req[0] (ctq);  R[0]->out_ack[0](cta);
    R[0]->out_flit[0](crf);  R[0]->out_req[0](crq);  R[0]->in_ack[0] (cra);
    ctrl.flit_tx(ctf); ctrl.req_tx(ctq); ctrl.ack_tx(cta);
    ctrl.flit_rx(crf); ctrl.req_rx(crq); ctrl.ack_rx(cra);

    // Core 0-15 <-> Router 0-15 LOCAL (port 4)
    for (int i=0; i<16; i++) {
        R[i]->out_flit[4](lrf[i]); R[i]->out_req[4](lrq[i]); R[i]->in_ack[4](lra[i]);
        R[i]->in_flit[4] (lf[i]);  R[i]->in_req[4] (lq[i]);  R[i]->out_ack[4](la[i]);
        C[i]->flit_tx(lf[i]);  C[i]->req_tx(lq[i]);  C[i]->ack_tx(la[i]);
        C[i]->flit_rx(lrf[i]); C[i]->req_rx(lrq[i]); C[i]->ack_rx(lra[i]);
    }

    sc_start();

    for (int i=0; i<16; i++) delete R[i];
    for (int i=0; i<16; i++) delete C[i];
    return 0;
}