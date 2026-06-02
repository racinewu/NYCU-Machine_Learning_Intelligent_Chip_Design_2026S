#include <systemc.h>
#include "producer.h"
#include "forwarder.h"
#include "checker.h"

int sc_main(int argc, char* argv[]) {
    // Signals
    sc_clock ACLK("ACLK", 10, SC_NS, 0.5, 0, SC_NS, true);
    sc_signal<bool> ARESETn;

    // Signals for Producer to Forwarder link
    sc_signal<bool>       p_f_tvalid;
    sc_signal<bool>       p_f_tready;
    sc_signal<char>       p_f_tdata;
    sc_signal<bool>       p_f_tlast;

    // Signals for Forwarder to Checker link
    sc_signal<bool>       f_c_tvalid;
    sc_signal<bool>       f_c_tready;
    sc_signal<char>       f_c_tdata;
    sc_signal<bool>       f_c_tlast;

    sc_signal<unsigned int> fwd_prod_req_id;
    sc_signal<bool>         fwd_prod_req_valid;
    sc_signal<bool>         prod_fwd_req_ready;

    // Instantiate modules
    Producer producer("Producer_inst");
    producer.ACLK(ACLK);
    producer.ARESETn(ARESETn);
    producer.M_AXIS_TVALID(p_f_tvalid);
    producer.M_AXIS_TREADY(p_f_tready);
    producer.M_AXIS_TDATA(p_f_tdata);
    producer.M_AXIS_TLAST(p_f_tlast);

    producer.REQ_ID_FROM_FWD(fwd_prod_req_id);
    producer.REQ_VALID_FROM_FWD(fwd_prod_req_valid);
    producer.REQ_READY_TO_FWD(prod_fwd_req_ready);

    Forwarder forwarder("Forwarder_inst");
    forwarder.ACLK(ACLK);
    forwarder.ARESETn(ARESETn);
    // Slave port of Forwarder
    forwarder.S_AXIS_TVALID(p_f_tvalid);
    forwarder.S_AXIS_TREADY(p_f_tready);
    forwarder.S_AXIS_TDATA(p_f_tdata);
    forwarder.S_AXIS_TLAST(p_f_tlast);
    // Master port of Forwarder
    forwarder.M_AXIS_TVALID(f_c_tvalid);
    forwarder.M_AXIS_TREADY(f_c_tready);
    forwarder.M_AXIS_TDATA(f_c_tdata);
    forwarder.M_AXIS_TLAST(f_c_tlast);

    forwarder.REQ_ID_TO_PROD(fwd_prod_req_id);
    forwarder.REQ_VALID_TO_PROD(fwd_prod_req_valid);
    forwarder.REQ_READY_FROM_PROD(prod_fwd_req_ready);

    Checker checker("Checker_inst");
    checker.ACLK(ACLK);
    checker.ARESETn(ARESETn);
    checker.S_AXIS_TVALID(f_c_tvalid);
    checker.S_AXIS_TREADY(f_c_tready);
    checker.S_AXIS_TDATA(f_c_tdata);
    checker.S_AXIS_TLAST(f_c_tlast);

    // Simulation control
    cout << "Simulation starting..." << endl;

    // Initial reset
    ARESETn.write(true);
    sc_start(1, SC_NS); 
    ARESETn.write(false); 
    cout << "@ " << sc_time_stamp() << " Asserting reset (ARESETn = 0)" << endl;
    sc_start(10, SC_NS);
    ARESETn.write(true);
    cout << "@ " << sc_time_stamp() << " De-asserting reset (ARESETn = 1)" << endl;
    sc_start(1, SC_NS);

    sc_start(1000, SC_NS); 

    cout << "Simulation finished at " << sc_time_stamp() << endl;

    return 0;
}