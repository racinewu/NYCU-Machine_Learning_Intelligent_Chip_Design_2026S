#ifndef SRAM_H
#define SRAM_H

// ============================================================
// On-chip SRAM Behavior Model
//
// Specification (Baseline):
//   - Single-port SRAM (one read or one write per access)
//   - Read  latency : 1 cycle (pipeline registered output)
//   - Write latency : 1 cycle
//   - Data width    : 32 bits (1 float per access),
//                     or burst up to capacity via DMA
//   - Capacity      : 128 KB (32768 floats) [baseline]
//                     Optimized design uses larger capacity.
//   - Banks         : 1 (baseline single-buffer design)
//   - Purpose       : staging buffer between AXI DMA and PE array
//
// Feature map spill policy:
//   If feature map size <= SRAM capacity: kept on-chip (no DRAM write).
//   If feature map size >  SRAM capacity: spilled to DRAM intermediate
//   region and reloaded before next layer. This is managed by
//   Controller::fm_store() / fm_load().
//
// With 128 KB SRAM, Conv1 output (182 KB), Conv3 output (254 KB),
// Conv4 output (169 KB) all spill to DRAM. Conv2 (127 KB), Conv5
// (36 KB), FC outputs (16 KB) stay on-chip.
// ============================================================

#include <vector>
#include <cassert>
#include <cstring>
#include <iostream>
#include <sstream>
#include <iomanip>

#define SRAM_READ_LATENCY   1   // cycles per access
#define SRAM_WRITE_LATENCY  1   // cycles per access
#define SRAM_DEFAULT_FLOATS 32768   // 128 KB (32768 floats × 4 bytes)

class SRAM {
public:
    explicit SRAM(int capacity_floats = SRAM_DEFAULT_FLOATS)
        : capacity_(capacity_floats), mem_(capacity_floats, 0.f) {}

    void write(int offset, const float* data, int n) {
        assert(offset >= 0 && offset + n <= capacity_);
        std::memcpy(&mem_[offset], data, (size_t)n * sizeof(float));
        write_count_ += n;
    }
    void write(int offset, const std::vector<float>& v) {
        write(offset, v.data(), (int)v.size());
    }

    void read(int offset, float* out, int n) const {
        assert(offset >= 0 && offset + n <= capacity_);
        std::memcpy(out, &mem_[offset], (size_t)n * sizeof(float));
        read_count_ += n;
    }
    std::vector<float> read(int offset, int n) const {
        std::vector<float> out(n);
        read(offset, out.data(), n);
        return out;
    }

    int capacity() const { return capacity_; }

    void reset_stats() { read_count_ = 0; write_count_ = 0; }
    long long read_count()  const { return read_count_;  }
    long long write_count() const { return write_count_; }

    void print_stats() const {
        auto fmt = [](long long floats) -> std::string {
            long long bytes = floats * 4;
            std::ostringstream s;
            s << std::fixed << std::setprecision(2);
            if (bytes >= 1024*1024)      s << bytes/1024.0/1024.0 << " MB";
            else if (bytes >= 1024)      s << bytes/1024.0        << " KB";
            else                         s << bytes               << " B";
            return s.str();
        };
        std::cout << "[SRAM] Capacity     : "
                  << capacity_ * 4 / 1024 << " KB ("
                  << capacity_ << " floats)" << std::endl;
        std::cout << "[SRAM] Read  volume : "
                  << fmt(read_count_)  << " (" << read_count_  << " floats)" << std::endl;
        std::cout << "[SRAM] Write volume : "
                  << fmt(write_count_) << " (" << write_count_ << " floats)" << std::endl;
    }

private:
    int           capacity_;
    std::vector<float> mem_;
    mutable long long read_count_  = 0;
    mutable long long write_count_ = 0;
};

#endif // SRAM_H
