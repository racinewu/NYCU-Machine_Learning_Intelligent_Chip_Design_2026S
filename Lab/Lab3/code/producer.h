#ifndef PRODUCER_ADDR_SC_THREAD_V2_H
#define PRODUCER_ADDR_SC_THREAD_V2_H

#include <systemc.h>
#include <vector>
#include <string>
#include <iostream>

static std::vector<std::string> internal_initialize_producer_strings_sc_thread_v2() {
    std::vector<std::string> temp_vec;
    // temp_vec.push_back("HELLO");
    // temp_vec.push_back("NYCU");
    temp_vec.push_back("MLCHIP");
    return temp_vec;
}

SC_MODULE(Producer) {
    // Ports
    sc_in<bool> ACLK;
    sc_in<bool> ARESETn; // Active-low reset

    // --- Request Interface (Slave) ---
    sc_in<unsigned int> REQ_ID_FROM_FWD;
    sc_in<bool>         REQ_VALID_FROM_FWD;
    sc_out<bool>        REQ_READY_TO_FWD;

    // --- AXI Stream Data Interface (Master) ---
    sc_out<bool>        M_AXIS_TVALID;
    sc_in<bool>         M_AXIS_TREADY;
    sc_out<char>        M_AXIS_TDATA;
    sc_out<bool>        M_AXIS_TLAST;

    const std::vector<std::string> strings_to_send_list;

    void producer_thread_logic() {
        REQ_READY_TO_FWD.write(true);
        M_AXIS_TVALID.write(false);
        M_AXIS_TDATA.write('\0');
        M_AXIS_TLAST.write(false);

        unsigned int current_req_id = 0;
        unsigned int char_idx = 0;
        bool busy_sending_data = false;

        while (true) {
            wait();

            if (!ARESETn.read()) {
                REQ_READY_TO_FWD.write(true);
                M_AXIS_TVALID.write(false);
                M_AXIS_TDATA.write('\0');
                M_AXIS_TLAST.write(false);
                
                current_req_id = 0;
                char_idx = 0;
                busy_sending_data = false;
                continue;
            }

            // --- Read inputs for this cycle ---
            bool req_valid_input = REQ_VALID_FROM_FWD.read();
            unsigned int req_id_input = REQ_ID_FROM_FWD.read(); 
            bool m_axis_tready_input = M_AXIS_TREADY.read();

            // --- Logic based on inputs and current state ---
            bool next_busy_sending_data = busy_sending_data;
            unsigned int next_current_req_id = current_req_id;
            unsigned int next_char_idx = char_idx;
            
            bool drive_req_ready = true; 
            char drive_m_tdata = '\0';
            bool drive_m_tlast = false;
            bool drive_m_tvalid = false;

            // --- Handle Request Acceptance ---
            if (!busy_sending_data) {
                if (req_valid_input) { 
                    next_current_req_id = req_id_input;
                    next_char_idx = 0;
                    next_busy_sending_data = true;
                    drive_req_ready = false; 
                    std::cout << sc_time_stamp() << ": Producer: Accepted request for string ID "
                              << next_current_req_id << ". Becoming busy." << std::endl;
                } else {
                    drive_req_ready = true;
                }
            } else {
                drive_req_ready = false;
            }

            // --- Handle Data Transmission ---
            if (busy_sending_data) { 
                if (current_req_id < strings_to_send_list.size()) {
                    const std::string& string_to_send = strings_to_send_list[current_req_id];
                    if (char_idx < string_to_send.length()) {
                        drive_m_tdata = string_to_send[char_idx];
                        drive_m_tlast = (char_idx == (string_to_send.length() - 1));
                        drive_m_tvalid = true;

                        std::cout << sc_time_stamp() << ": Producer: Driving data '"
                                  << drive_m_tdata << "' (TLAST=" << drive_m_tlast 
                                  << ") for ID " << current_req_id << std::endl;
                        
                        if (m_axis_tready_input) { 
                            std::cout << sc_time_stamp() << ": Producer: Data '"
                                      << drive_m_tdata << "' sent (handshake)." << std::endl;
                            next_char_idx = char_idx + 1;
                            if (next_char_idx >= string_to_send.length()) {
                                next_busy_sending_data = false;
                                std::cout << sc_time_stamp() << ": Producer: Finished sending string ID "
                                          << current_req_id << ". Ready for new request." << std::endl;
                            }
                        } else {
                            std::cout << sc_time_stamp() << ": Producer: Stalling data send for '"
                                      << drive_m_tdata << "', M_AXIS_TREADY low." << std::endl;
                        }
                    } else { 
                        next_busy_sending_data = false; // String finished
                        drive_m_tvalid = false; 
                        drive_m_tlast = false;
                        std::cout << sc_time_stamp() << ": Producer: String finished (char_idx at end), transitioning to not busy." << std::endl;
                    }
                } else { 
                    std::cout << sc_time_stamp() << ": Producer: Error - Invalid string ID "
                              << current_req_id << " being processed." << std::endl;
                    next_busy_sending_data = false;
                    drive_m_tvalid = false;
                    drive_m_tlast = false;
                }
            } else {
                drive_m_tvalid = false; 
                drive_m_tlast = false;
            }

            // --- Update internal states for the next cycle ---
            busy_sending_data = next_busy_sending_data;
            current_req_id = next_current_req_id;
            char_idx = next_char_idx;

            // --- Drive outputs for this cycle ---
            REQ_READY_TO_FWD.write(drive_req_ready);
            M_AXIS_TDATA.write(drive_m_tdata);
            M_AXIS_TLAST.write(drive_m_tlast);
            M_AXIS_TVALID.write(drive_m_tvalid);

        }
    }

    SC_CTOR(Producer) :
        strings_to_send_list(internal_initialize_producer_strings_sc_thread_v2())
    {
        SC_THREAD(producer_thread_logic);
        sensitive << ACLK.pos(); 
        sensitive << ARESETn.neg(); 
    }
};

#endif // PRODUCER_ADDR_SC_THREAD_V2_H
