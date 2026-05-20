#ifndef CHECKER_H
#define CHECKER_H

#include <systemc.h>
#include <string>
#include <vector>

static std::vector<std::string> internal_initialize_checker_strings() {
    std::vector<std::string> temp_vec;
    temp_vec.push_back("HELLO");
    temp_vec.push_back("NYCU");
    temp_vec.push_back("MLCHIP");
    return temp_vec;
}

SC_MODULE(Checker) {
    // Ports
    sc_in<bool> ACLK;
    sc_in<bool> ARESETn;

    sc_in<bool>         S_AXIS_TVALID;
    sc_out<bool>        S_AXIS_TREADY;
    sc_in<char>         S_AXIS_TDATA;
    sc_in<bool>         S_AXIS_TLAST;

    std::string received_string_buffer;
    const std::vector<std::string> expected_strings; 
    unsigned int expected_string_index;
    bool all_strings_checked;
    int correct_string_count;
    int incorrect_string_count;

    void receive_data_proc() {
        if (!ARESETn.read()) {
            S_AXIS_TREADY.write(true);
            received_string_buffer.clear();
            expected_string_index = 0;
            all_strings_checked = false;
            cout << sc_time_stamp() << ": Checker: Resetting..." << endl;
            correct_string_count = 0;
            incorrect_string_count = 0;
        }

        if (all_strings_checked) {
            S_AXIS_TREADY.write(false);
        }

        S_AXIS_TREADY.write(true);

        if (S_AXIS_TVALID.read() && S_AXIS_TREADY.read()) {
            char received_char = S_AXIS_TDATA.read();
            received_string_buffer += received_char;
            // Optional: cout for debugging
            cout << sc_time_stamp() << ": Checker: Received '" << received_char
                 << "' (TLAST=" << S_AXIS_TLAST.read() << ")" << endl;

            if (S_AXIS_TLAST.read()) {
                cout << sc_time_stamp() << ": Checker: Full string received: \"" << received_string_buffer << "\"" << endl;
                if (expected_string_index < expected_strings.size()) {
                    if (received_string_buffer == expected_strings[expected_string_index]) {
                        cout << sc_time_stamp() << ": Checker: String CORRECT! Expected: \""
                             << expected_strings[expected_string_index] << "\"" << endl;
                        correct_string_count++;
                    } else {
                        cout << sc_time_stamp() << ": Checker: String INCORRECT! Expected: \""
                             << expected_strings[expected_string_index] << "\", Got: \""
                             << received_string_buffer << "\"" << endl;
                        incorrect_string_count++;
                    }
                    expected_string_index++;
                    if (expected_string_index >= expected_strings.size()) {
                        if(incorrect_string_count > 0) {
                            cout << sc_time_stamp() << ": Checker: All expected strings processed. "
                                 << "Total correct: " << correct_string_count
                                 << ", Total incorrect: " << incorrect_string_count << endl;
                        } else {
                            if(correct_string_count == 3){
                                cout << sc_time_stamp() << ": Checker: All expected strings processed. Congratulations." << endl;
                            }
                        }

                        all_strings_checked = true;
                        S_AXIS_TREADY.write(false);
                    }
                } else {
                    cout << sc_time_stamp() << ": Checker: Received unexpected string: \"" << received_string_buffer << "\"" << endl;
                }
                received_string_buffer.clear();
            }
        }
    }

    SC_CTOR(Checker) :
        expected_strings(internal_initialize_checker_strings()),
        expected_string_index(0),
        all_strings_checked(false)
    {
        SC_METHOD(receive_data_proc);
        sensitive << ACLK.pos() << ARESETn.neg();
        received_string_buffer.reserve(20);
    }
};

#endif // CHECKER_H