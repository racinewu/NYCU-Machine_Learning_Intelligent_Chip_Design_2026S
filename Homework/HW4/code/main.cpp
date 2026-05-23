#include "clockreset.h"
#include "core.h"
#include "router.h"
#include "controller.h"
#include "ROM.h"
#include "systemc.h"
#include <sstream>

// ============================================================
// 4x4 Mesh NoC:
//   R0 -- R1 -- R2 -- R3   (top row, active)
//   R4 .. R15              (all ports stubbed)
//
//   R0 = Controller    R1 = Core1    R2 = Core2    R3 = Core3
//   Ports: 0=North 1=South 2=East 3=West 4=Local
// ============================================================

// Stub signal pools (large enough for 16 routers * 5 ports)
sc_signal<sc_lv<34>> sf[80];   // flit stubs (in+out share same signal)
sc_signal<bool>      sb[320];  // bool stubs (4 per port)

int _sf = 0, _sb = 0;

void stub_port(Router* r, int p) {
    r->out_flit[p](sf[_sf]);
    r->in_flit[p] (sf[_sf]);   _sf++;
    r->out_req[p] (sb[_sb++]);
    r->in_req[p]  (sb[_sb++]);
    r->in_ack[p]  (sb[_sb++]);
    r->out_ack[p] (sb[_sb++]);
}

int sc_main(int argc, char* argv[])
{
    sc_signal<bool> clk, rst;

    Clock m_clock("m_clock", 10);
    Reset m_reset("m_reset", 15);
    m_clock(clk);
    m_reset(rst);

    // ROM signals
    sc_signal<int>   layer_id;
    sc_signal<bool>  layer_id_type, layer_id_valid;
    sc_signal<float> rom_data;
    sc_signal<bool>  rom_data_valid;

    ROM m_rom("m_rom");
    m_rom.clk(clk); m_rom.rst(rst);
    m_rom.layer_id(layer_id);
    m_rom.layer_id_type(layer_id_type);
    m_rom.layer_id_valid(layer_id_valid);
    m_rom.data(rom_data);
    m_rom.data_valid(rom_data_valid);

    // Routers
    Router* routers[16];
    for (int i = 0; i < 16; i++) {
        std::ostringstream oss; oss << "router_" << i;
        routers[i] = new Router(oss.str().c_str());
        routers[i]->init(i);
        routers[i]->clk(clk);
        routers[i]->rst(rst);
    }

    // Cores 1..3
    Core* cores[4];
    for (int i = 1; i <= 3; i++) {
        std::ostringstream oss; oss << "core_" << i;
        cores[i] = new Core(oss.str().c_str());
        cores[i]->init(i, "./data");
        cores[i]->clk(clk);
        cores[i]->rst(rst);
    }

    // Controller
    Controller m_ctrl("controller");
    m_ctrl.clk(clk); m_ctrl.rst(rst);
    m_ctrl.layer_id(layer_id);
    m_ctrl.layer_id_type(layer_id_type);
    m_ctrl.layer_id_valid(layer_id_valid);
    m_ctrl.data(rom_data);
    m_ctrl.data_valid(rom_data_valid);

    // -------------------------------------------------------
    // Inter-router East/West links (R0-R1-R2-R3)
    // -------------------------------------------------------
    sc_signal<sc_lv<34>> r01f, r10f, r12f, r21f, r23f, r32f;
    sc_signal<bool>      r01req, r01ack, r10req, r10ack;
    sc_signal<bool>      r12req, r12ack, r21req, r21ack;
    sc_signal<bool>      r23req, r23ack, r32req, r32ack;

    // Controller <-> R0 LOCAL
    sc_signal<sc_lv<34>> ctrl_tx_f, ctrl_rx_f;
    sc_signal<bool>      ctrl_tx_req, ctrl_tx_ack, ctrl_rx_req, ctrl_rx_ack;

    // Core1 <-> R1 LOCAL
    sc_signal<sc_lv<34>> c1_tx_f, c1_rx_f;
    sc_signal<bool>      c1_tx_req, c1_tx_ack, c1_rx_req, c1_rx_ack;

    // Core2 <-> R2 LOCAL
    sc_signal<sc_lv<34>> c2_tx_f, c2_rx_f;
    sc_signal<bool>      c2_tx_req, c2_tx_ack, c2_rx_req, c2_rx_ack;

    // Core3 <-> R3 LOCAL
    sc_signal<sc_lv<34>> c3_tx_f, c3_rx_f;
    sc_signal<bool>      c3_tx_req, c3_tx_ack, c3_rx_req, c3_rx_ack;

    // -------------------------------------------------------
    // Wire Router 0  (N=stub S=stub E=R1 W=stub L=Ctrl)
    // -------------------------------------------------------
    stub_port(routers[0], 0);  // North
    stub_port(routers[0], 1);  // South
    // East -> R1 West
    routers[0]->out_flit[2](r01f);    routers[0]->out_req[2](r01req); routers[0]->in_ack[2](r01ack);
    routers[0]->in_flit[2] (r10f);    routers[0]->in_req[2](r10req);  routers[0]->out_ack[2](r10ack);
    stub_port(routers[0], 3);  // West
    // Local = Controller
    routers[0]->out_flit[4](ctrl_rx_f);  routers[0]->out_req[4](ctrl_rx_req); routers[0]->in_ack[4](ctrl_rx_ack);
    routers[0]->in_flit[4] (ctrl_tx_f);  routers[0]->in_req[4](ctrl_tx_req);  routers[0]->out_ack[4](ctrl_tx_ack);

    // -------------------------------------------------------
    // Wire Router 1  (N=stub S=stub E=R2 W=R0 L=Core1)
    // -------------------------------------------------------
    stub_port(routers[1], 0);  // North
    stub_port(routers[1], 1);  // South
    // East -> R2 West
    routers[1]->out_flit[2](r12f);    routers[1]->out_req[2](r12req); routers[1]->in_ack[2](r12ack);
    routers[1]->in_flit[2] (r21f);    routers[1]->in_req[2](r21req);  routers[1]->out_ack[2](r21ack);
    // West = R0 East
    routers[1]->out_flit[3](r10f);    routers[1]->out_req[3](r10req); routers[1]->in_ack[3](r10ack);
    routers[1]->in_flit[3] (r01f);    routers[1]->in_req[3](r01req);  routers[1]->out_ack[3](r01ack);
    // Local = Core1
    routers[1]->out_flit[4](c1_rx_f); routers[1]->out_req[4](c1_rx_req); routers[1]->in_ack[4](c1_rx_ack);
    routers[1]->in_flit[4] (c1_tx_f); routers[1]->in_req[4](c1_tx_req);  routers[1]->out_ack[4](c1_tx_ack);

    // -------------------------------------------------------
    // Wire Router 2  (N=stub S=stub E=R3 W=R1 L=Core2)
    // -------------------------------------------------------
    stub_port(routers[2], 0);  // North
    stub_port(routers[2], 1);  // South
    // East -> R3 West
    routers[2]->out_flit[2](r23f);    routers[2]->out_req[2](r23req); routers[2]->in_ack[2](r23ack);
    routers[2]->in_flit[2] (r32f);    routers[2]->in_req[2](r32req);  routers[2]->out_ack[2](r32ack);
    // West = R1 East
    routers[2]->out_flit[3](r21f);    routers[2]->out_req[3](r21req); routers[2]->in_ack[3](r21ack);
    routers[2]->in_flit[3] (r12f);    routers[2]->in_req[3](r12req);  routers[2]->out_ack[3](r12ack);
    // Local = Core2
    routers[2]->out_flit[4](c2_rx_f); routers[2]->out_req[4](c2_rx_req); routers[2]->in_ack[4](c2_rx_ack);
    routers[2]->in_flit[4] (c2_tx_f); routers[2]->in_req[4](c2_tx_req);  routers[2]->out_ack[4](c2_tx_ack);

    // -------------------------------------------------------
    // Wire Router 3  (N=stub S=stub E=stub W=R2 L=Core3)
    // -------------------------------------------------------
    stub_port(routers[3], 0);  // North
    stub_port(routers[3], 1);  // South
    stub_port(routers[3], 2);  // East
    // West = R2 East
    routers[3]->out_flit[3](r32f);    routers[3]->out_req[3](r32req); routers[3]->in_ack[3](r32ack);
    routers[3]->in_flit[3] (r23f);    routers[3]->in_req[3](r23req);  routers[3]->out_ack[3](r23ack);
    // Local = Core3
    routers[3]->out_flit[4](c3_rx_f); routers[3]->out_req[4](c3_rx_req); routers[3]->in_ack[4](c3_rx_ack);
    routers[3]->in_flit[4] (c3_tx_f); routers[3]->in_req[4](c3_tx_req);  routers[3]->out_ack[4](c3_tx_ack);

    // -------------------------------------------------------
    // Stub all ports of routers 4..15
    // -------------------------------------------------------
    for (int r = 4; r < 16; r++)
        for (int p = 0; p < 5; p++)
            stub_port(routers[r], p);

    // -------------------------------------------------------
    // Connect Controller
    // -------------------------------------------------------
    m_ctrl.flit_tx(ctrl_tx_f);   m_ctrl.req_tx(ctrl_tx_req); m_ctrl.ack_tx(ctrl_tx_ack);
    m_ctrl.flit_rx(ctrl_rx_f);   m_ctrl.req_rx(ctrl_rx_req); m_ctrl.ack_rx(ctrl_rx_ack);

    // -------------------------------------------------------
    // Connect Core1
    // -------------------------------------------------------
    cores[1]->flit_tx(c1_tx_f);  cores[1]->req_tx(c1_tx_req); cores[1]->ack_tx(c1_tx_ack);
    cores[1]->flit_rx(c1_rx_f);  cores[1]->req_rx(c1_rx_req); cores[1]->ack_rx(c1_rx_ack);

    // -------------------------------------------------------
    // Connect Core2
    // -------------------------------------------------------
    cores[2]->flit_tx(c2_tx_f);  cores[2]->req_tx(c2_tx_req); cores[2]->ack_tx(c2_tx_ack);
    cores[2]->flit_rx(c2_rx_f);  cores[2]->req_rx(c2_rx_req); cores[2]->ack_rx(c2_rx_ack);

    // -------------------------------------------------------
    // Connect Core3
    // -------------------------------------------------------
    cores[3]->flit_tx(c3_tx_f);  cores[3]->req_tx(c3_tx_req); cores[3]->ack_tx(c3_tx_ack);
    cores[3]->flit_rx(c3_rx_f);  cores[3]->req_rx(c3_rx_req); cores[3]->ack_rx(c3_rx_ack);

    sc_start();

    for (int i = 0; i < 16; i++) delete routers[i];
    for (int i = 1; i <= 3;  i++) delete cores[i];
    return 0;
}