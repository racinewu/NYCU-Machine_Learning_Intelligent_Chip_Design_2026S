#ifndef CHECKER_H
#define CHECKER_H

#include <systemc.h>
#include <string>
#include <vector>
#include <iostream> 

// Initialization function for strings expected by the Checker
static std::vector<std::string> internal_initialize_checker_strings() {
    std::vector<std::string> temp_vec;
    // temp_vec.push_back("HELLO");
    // temp_vec.push_back("NYCU");
    temp_vec.push_back("MLCHIP");
    return temp_vec;
}

SC_MODULE(Checker) {
    // Ports
    sc_in<bool> ACLK;
    sc_in<bool> ARESETn; // Active-low reset

    // AXI Slave Interface Ports
    sc_in<bool>     S_AXIS_TVALID;
    sc_out<bool>    S_AXIS_TREADY;
    sc_in<char>     S_AXIS_TDATA;
    sc_in<bool>     S_AXIS_TLAST;

    std::string received_string_buffer;
    const std::vector<std::string> expected_strings;
    unsigned int expected_string_index;
    bool all_strings_checked_and_done;
    unsigned int correct_string_count; 
    unsigned int incorrect_string_count; 


    void receive_data_proc() {
        if (!ARESETn.read()) {
            S_AXIS_TREADY.write(false);
            received_string_buffer.clear();
            expected_string_index = 0;
            all_strings_checked_and_done = false;
            correct_string_count = 0;  
            incorrect_string_count = 0; 
        } else {
            if (all_strings_checked_and_done) {
                S_AXIS_TREADY.write(false); 
            } else {
                S_AXIS_TREADY.write(true); 
            }

            if (S_AXIS_TVALID.read() && S_AXIS_TREADY.read()) {
                char received_char = S_AXIS_TDATA.read();
                bool is_last_char = S_AXIS_TLAST.read();
                received_string_buffer += received_char;

                std::cout << sc_time_stamp() << ": Checker: Received '" << received_char
                          << "' (TLAST=" << is_last_char 
                          << ", TVALID=" << S_AXIS_TVALID.read() 
                          << ", TREADY_driven=" << S_AXIS_TREADY.read() << ")" << std::endl;

                if (is_last_char) {
                    std::cout << sc_time_stamp() << ": Checker: Full string received: \"" << received_string_buffer << "\"" << std::endl;
                    if (expected_string_index < expected_strings.size()) {
                        if (received_string_buffer == expected_strings[expected_string_index]) {
                            std::cout << sc_time_stamp() << ": Checker: String CORRECT! Expected: \""
                                      << expected_strings[expected_string_index] << "\"" << std::endl;
                            correct_string_count++;
                        } else {
                            std::cout << sc_time_stamp() << ": Checker: String INCORRECT! Expected: \""
                                      << expected_strings[expected_string_index] << "\", Got: \""
                                      << received_string_buffer << "\"" << std::endl;
                            incorrect_string_count++; 
                        }
                        expected_string_index++;
                        if (expected_string_index >= expected_strings.size()) {
                            if (incorrect_string_count > 0) {
                                std::cout << sc_time_stamp() << ": Checker: All expected strings processed. "
                                          << "Total correct: " << correct_string_count
                                          << ", Total incorrect: " << incorrect_string_count << std::endl;
                            } else { 
                                if (correct_string_count == expected_strings.size() && correct_string_count > 0) { 
                                    std::cout << sc_time_stamp() << ": Checker: All expected strings processed. Congratulations." << std::endl;
                                } else { 
                                     std::cout << sc_time_stamp() << ": Checker: All expected strings processed. "
                                              << "Total correct: " << correct_string_count
                                              << ", Total incorrect: " << incorrect_string_count 
                                              << " (Note: All were expected to be correct for congratulations message)." << std::endl;
                                }
                            }
                            all_strings_checked_and_done = true;
                            S_AXIS_TREADY.write(false);
                        }
                    } else {

                        std::cout << sc_time_stamp() << ": Checker: Received unexpected string (all expected strings already processed): \""
                                  << received_string_buffer << "\"" << std::endl;
                        incorrect_string_count++;
                        all_strings_checked_and_done = true;
                        S_AXIS_TREADY.write(false);
                    }
                    received_string_buffer.clear(); 
                }
            }
        }
    }

    SC_CTOR(Checker) :
        expected_strings(internal_initialize_checker_strings()),
        expected_string_index(0),
        all_strings_checked_and_done(false),
        correct_string_count(0),  
        incorrect_string_count(0) 
    {
        SC_METHOD(receive_data_proc);
        sensitive << ACLK.pos();
        sensitive << ARESETn.neg();
        received_string_buffer.reserve(30);
        S_AXIS_TREADY.initialize(false);
    }
};

#endif // CHECKER_H
