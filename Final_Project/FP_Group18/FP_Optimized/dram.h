#ifndef DRAM_H
#define DRAM_H

// ==================================================
// DRAM Memory Map (byte-addressed, 4 bytes per float)
//
//   Region          | Base Address  | Size (floats)
//   ----------------+---------------+----------------
//   Image           | 0x00000000    | 150528   (3*224*224)
//   Conv1 weight    | 0x00100000    | 23232    (64*3*11*11)
//   Conv1 bias      | 0x00120000    | 64
//   Conv2 weight    | 0x00130000    | 307200   (192*64*5*5)
//   Conv2 bias      | 0x00260000    | 192
//   Conv3 weight    | 0x00270000    | 663552   (384*192*3*3)
//   Conv3 bias      | 0x00540000    | 384
//   Conv4 weight    | 0x00550000    | 884736   (256*384*3*3)
//   Conv4 bias      | 0x008D0000    | 256
//   Conv5 weight    | 0x008E0000    | 589824   (256*256*3*3)
//   Conv5 bias      | 0x00D20000    | 256
//   FC6 weight      | 0x00D30000    | 37748736 (4096*9216)
//   FC6 bias        | 0x09D30000    | 4096
//   FC7 weight      | 0x09D70000    | 16777216 (4096*4096)
//   FC7 bias        | 0x0DD70000    | 4096
//   FC8 weight      | 0x0DDB0000    | 4096000  (1000*4096)
//   FC8 bias        | 0x11DB0000    | 1000
//   Intermediate    | 0x12000000    | 8192000  (spare)
//   Output          | 0x14000000    | 2000     (lin+softmax)
//
// Total: ~0x15000000 = 352 MB
// ==================================================

#include <vector>
#include <cstring>
#include <cassert>
#include <iostream>
#include <sstream>
#include <iomanip>

#define DRAM_IMAGE_BASE       0x00000000U
#define DRAM_C1W_BASE         0x00100000U
#define DRAM_C1B_BASE         0x00120000U
#define DRAM_C2W_BASE         0x00130000U
#define DRAM_C2B_BASE         0x00260000U
#define DRAM_C3W_BASE         0x00270000U
#define DRAM_C3B_BASE         0x00540000U
#define DRAM_C4W_BASE         0x00550000U
#define DRAM_C4B_BASE         0x008D0000U
#define DRAM_C5W_BASE         0x008E0000U
#define DRAM_C5B_BASE         0x00D20000U
#define DRAM_FC6W_BASE        0x00D30000U
#define DRAM_FC6B_BASE        0x09D30000U
#define DRAM_FC7W_BASE        0x09D70000U
#define DRAM_FC7B_BASE        0x0DD70000U
#define DRAM_FC8W_BASE        0x0DDB0000U
#define DRAM_FC8B_BASE        0x11DB0000U
#define DRAM_INTER_BASE       0x12000000U
#define DRAM_OUTPUT_BASE      0x14000000U
#define DRAM_TOTAL_BYTES      0x15000000U   // 352 MB

// ==================================================
// DRAM behavior model:
//   - Byte-addressable flat array
//   - Burst access: read/write n floats at 4-byte aligned addr
//   - Latency: modeled externally by AXI DMA beat counter
//   - No port-level gate model (behavioral abstraction)
// ==================================================
class DRAM {
public:
    DRAM() { mem_.resize(DRAM_TOTAL_BYTES, 0); }

    void write_floats(unsigned int addr, const float* data, int n) {
        assert(addr + (unsigned)(n*4) <= DRAM_TOTAL_BYTES);
        std::memcpy(&mem_[addr], data, (size_t)n * sizeof(float));
        write_count_ += n;
    }
    void write_floats(unsigned int addr, const std::vector<float>& v) {
        write_floats(addr, v.data(), (int)v.size());
    }

    void read_floats(unsigned int addr, float* out, int n) const {
        assert(addr + (unsigned)(n*4) <= DRAM_TOTAL_BYTES);
        std::memcpy(out, &mem_[addr], (size_t)n * sizeof(float));
        read_count_ += n;
    }
    std::vector<float> read_floats(unsigned int addr, int n) const {
        std::vector<float> out(n);
        read_floats(addr, out.data(), n);
        return out;
    }

    void reset_stats() { read_count_ = 0; write_count_ = 0; }
    long long read_count()  const { return read_count_;  }
    long long write_count() const { return write_count_; }

    void print_stats() const {
        auto fmt = [](long long floats) -> std::string {
            long long bytes = floats * 4;
            std::ostringstream s;
            s << std::fixed << std::setprecision(2);
            if (bytes >= 1024*1024)
                s << bytes / 1024.0 / 1024.0 << " MB";
            else if (bytes >= 1024)
                s << bytes / 1024.0 << " KB";
            else
                s << bytes << " B";
            return s.str();
        };
        std::cout << "[DRAM] Read  volume : "
                  << fmt(read_count_)  << " (" << read_count_  << " floats)" << std::endl;
        std::cout << "[DRAM] Write volume : "
                  << fmt(write_count_) << " (" << write_count_ << " floats)" << std::endl;
    }

private:
    std::vector<unsigned char> mem_;
    mutable long long read_count_  = 0;
    mutable long long write_count_ = 0;
};

#endif // DRAM_H
