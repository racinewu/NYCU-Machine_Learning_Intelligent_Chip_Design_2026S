#ifndef PRODUCER_H
#define PRODUCER_H

#include <systemc.h>
#include <vector>
#include <string>

static std::vector<std::string> internal_initialize_producer_strings() {
    std::vector<std::string> temp_vec;
    temp_vec.push_back("HELLO");
    temp_vec.push_back("NYCU");
    temp_vec.push_back("MLCHIP");
    return temp_vec;
}

SC_MODULE(Producer) {
    // Ports
    sc_in<bool> ACLK;
    sc_in<bool> ARESETn;

    sc_out<bool>        M_AXIS_TVALID;
    sc_in<bool>         M_AXIS_TREADY;
    sc_out<char>        M_AXIS_TDATA;  
    sc_out<bool>        M_AXIS_TLAST;

    const std::vector<std::string> strings_to_send;
    unsigned int current_string_index;
    unsigned int current_char_index;
    bool sending_active;

    // Processes
    void send_data_proc() {
        // Reset logic
        if (!ARESETn.read()) {
            M_AXIS_TVALID.write(false);
            M_AXIS_TDATA.write('\0');
            M_AXIS_TLAST.write(false);
            current_string_index = 0;
            current_char_index = 0;
            sending_active = true;
            cout << sc_time_stamp() << ": Producer: Resetting..." << endl;
        }

        else if (sending_active && current_string_index < strings_to_send.size()) {
            const std::string& current_string = strings_to_send[current_string_index];
            if (current_char_index < current_string.length()) {
                if (M_AXIS_TREADY.read()) {
                    M_AXIS_TDATA.write(current_string[current_char_index]);
                    M_AXIS_TVALID.write(true);
                    M_AXIS_TLAST.write(current_char_index == (current_string.length() - 1));
                    bool tlast_value_being_written = (current_char_index == (current_string.length() - 1));
                    // Optional: cout for debugging
                    cout << sc_time_stamp() << ": Producer: Sent '"
                         << current_string[current_char_index]
                         << "' (TLAST=" << tlast_value_being_written << ")" << endl;
                    current_char_index++;
                } else {
                    M_AXIS_TVALID.write(false);
                }
            } else {
                M_AXIS_TVALID.write(false);
                M_AXIS_TLAST.write(false);
                current_char_index = 0;
                current_string_index++;
                if (current_string_index >= strings_to_send.size()) {
                    cout << sc_time_stamp() << ": Producer: All strings sent." << endl;
                    sending_active = false;
                } else {
                    cout << sc_time_stamp() << ": Producer: Moving to next string: " << strings_to_send[current_string_index] << endl;
                }
            }
        } else {
            M_AXIS_TVALID.write(false);
            M_AXIS_TLAST.write(false);
        }
    }

    SC_CTOR(Producer) :
        strings_to_send(internal_initialize_producer_strings()),
        current_string_index(0),
        current_char_index(0),
        sending_active(false)
    {
        SC_METHOD(send_data_proc);
        sensitive << ACLK.pos() << ARESETn.neg();
    }
};

#endif // PRODUCER_H