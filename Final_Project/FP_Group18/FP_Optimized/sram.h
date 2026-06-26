#ifndef SRAM_H
#define SRAM_H

// ==================================================
// On-chip SRAM Behavior Model — FP_Optimized
//
// 4-bank design, 128 KB per bank, 512 KB total.
//
// Conv stage bank roles:
//   Bank 0 (INPUT_PING) : Input FM buffer — written from controller, read by PE via NoC
//   Bank 1 (INPUT_PONG) : Not used in Conv stage (OS output accumulates in PE local SRAM)
//   Bank 2 (OUTPUT)     : Not used in Conv stage
//   Bank 3 (WEIGHT)     : Weight tile DMA staging (chunked load for each layer)
//
// FC stage bank roles:
//   Bank 0 (INPUT_PING) : Input activation locked (<=36KB, stays whole inference)
//   Bank 1 (INPUT_PONG) : Weight tile Ping (PE reads while DMA loads Bank 2)
//   Bank 2 (OUTPUT)     : Weight tile Pong (DMA loads while PE reads Bank 1)
//   Bank 3 (WEIGHT)     : Bias buffer
//
// Key benefit: FC weight tile Ping-Pong overlaps DMA load with PE compute.
// Each weight tile = 32768 floats = 128 KB = exactly one bank.
// Bank independence eliminates read-write conflicts between threads.
// ==================================================

#include <vector>
#include <cassert>
#include <cstring>
#include <iostream>
#include <sstream>
#include <iomanip>
#include "config.h"

#define SRAM_READ_LATENCY   1
#define SRAM_WRITE_LATENCY  1

// Bank indices
#define SRAM_BANK_INPUT_PING  0
#define SRAM_BANK_INPUT_PONG  1
#define SRAM_BANK_OUTPUT      2
#define SRAM_BANK_WEIGHT      3

class SRAM {
public:
    explicit SRAM(int n_banks = SRAM_NUM_BANKS,
                  int floats_per_bank = SRAM_BANK_FLOATS)
        : n_banks_(n_banks), bank_cap_(floats_per_bank)
    {
        mem_.resize((size_t)n_banks * floats_per_bank, 0.f);
    }

    // Write n floats to specified bank at float-index offset within bank
    void write(int bank, int offset, const float* data, int n) {
        assert(bank >= 0 && bank < n_banks_);
        assert(offset >= 0 && offset + n <= bank_cap_);
        std::memcpy(&mem_[(size_t)bank * bank_cap_ + offset],
                    data, (size_t)n * sizeof(float));
        write_count_ += n;
    }
    void write(int bank, int offset, const std::vector<float>& v) {
        write(bank, offset, v.data(), (int)v.size());
    }
    // Convenience: write whole vector from offset 0
    void write(int bank, const std::vector<float>& v) {
        write(bank, 0, v.data(), (int)v.size());
    }

    // Read n floats from specified bank at float-index offset
    void read(int bank, int offset, float* out, int n) const {
        assert(bank >= 0 && bank < n_banks_);
        assert(offset >= 0 && offset + n <= bank_cap_);
        std::memcpy(out,
                    &mem_[(size_t)bank * bank_cap_ + offset],
                    (size_t)n * sizeof(float));
        read_count_ += n;
    }
    std::vector<float> read(int bank, int offset, int n) const {
        std::vector<float> out(n);
        read(bank, offset, out.data(), n);
        return out;
    }
    std::vector<float> read(int bank, int n) const {
        return read(bank, 0, n);
    }

    int n_banks()   const { return n_banks_;  }
    int bank_cap()  const { return bank_cap_; }
    int capacity()  const { return n_banks_ * bank_cap_; }

    void reset_stats() { read_count_ = 0; write_count_ = 0; }
    long long read_count()  const { return read_count_;  }
    long long write_count() const { return write_count_; }

    void print_stats() const {
        auto fmt = [](long long floats) -> std::string {
            long long bytes = floats * 4;
            std::ostringstream s; s << std::fixed << std::setprecision(2);
            if (bytes >= 1024*1024) s << bytes/1024.0/1024.0 << " MB";
            else if (bytes >= 1024) s << bytes/1024.0        << " KB";
            else                    s << bytes               << " B";
            return s.str();
        };
        std::cout << "[SRAM] Read  volume : "
                  << fmt(read_count_)  << " (" << read_count_  << " floats)" << std::endl;
        std::cout << "[SRAM] Write volume : "
                  << fmt(write_count_) << " (" << write_count_ << " floats)" << std::endl;
    }

private:
    int n_banks_;
    int bank_cap_;
    std::vector<float> mem_;
    mutable long long read_count_  = 0;
    mutable long long write_count_ = 0;
};

#endif // SRAM_H
