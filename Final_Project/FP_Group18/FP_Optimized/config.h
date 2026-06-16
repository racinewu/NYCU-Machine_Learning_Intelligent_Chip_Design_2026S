#ifndef CONFIG_H
#define CONFIG_H

// ============================================================
// Architecture configuration — FP_Optimized
// ============================================================

// Number of cores for each phase
#define CORES_CONV1   4    // Conv1: 64 output ch / 4  = 16 ch/core
#define CORES_CONV2  16    // Conv2: 192 / 16 = 12 ch/core
#define CORES_CONV3  16    // Conv3: 384 / 16 = 24 ch/core
#define CORES_CONV4  16    // Conv4: 256 / 16 = 16 ch/core
#define CORES_CONV5  16    // Conv5: 256 / 16 = 16 ch/core
#define CORES_FC6    16    // FC6:  4096 / 16 = 256 neurons/core  [OPT: 8->16]
#define CORES_FC7    16    // FC7:  4096 / 16 = 256 neurons/core  [OPT: 8->16]
#define CORE_FC8     15    // FC8+Softmax: single core (ID 15)

// MAC units per PE                                            [OPT: 1->16]
#define MAC_PER_PE   16

// Clock period
#define CLK_PERIOD_NS 10

// ============================================================
// On-chip SRAM: 4 banks x 128 KB                            [OPT: 1x128KB->4x128KB]
//
// Conv stage:
//   Bank 0: Input FM buffer  (current layer input, on-chip, no DRAM spill)
//   Bank 1: Output partial sum (modeled by PE local SRAM, bank reserved)
//   Bank 2: Spare
//   Bank 3: Weight tile buffer (DMA staging, chunked load)
//
// FC stage:
//   Bank 0: Input activation (locked: flat9216=36KB / fc6out=16KB / fc7out=16KB)
//   Bank 1: Weight tile Ping  (PE reads this tile)
//   Bank 2: Weight tile Pong  (DMA loads next tile, overlaps with PE compute)
//   Bank 3: Bias buffer
//
// Conv FM Ping-Pong not applicable: Conv3 output = 254KB > 128KB per bank.
// FC weight Ping-Pong fully implemented: each tile = 32KB = exactly 1 bank.
// ============================================================
#define SRAM_NUM_BANKS   4
#define SRAM_BANK_FLOATS 32768   // 128 KB per bank (32768 floats)

// Local SRAM per PE: partial sum buffer for OS dataflow      [OPT: 0->16KB]
// Sized for largest output FM per PE: Conv3 24ch*13*13=4056 floats -> round to 4096
#define LOCAL_SRAM_FLOATS 4096   // 16 KB per PE (Conv2-5 partial sum; Conv1 bypasses)

// ============================================================
// Log level
//   0 = silent
//   1 = phase-level
//   2 = tile-level
//   3 = flit-level
// ============================================================
#define LOG_LEVEL 2

#include <iostream>
#include <iomanip>
#define LOG1(msg) do { if (LOG_LEVEL >= 1) { std::cout << msg << std::endl; } } while(0)
#define LOG2(msg) do { if (LOG_LEVEL >= 2) { std::cout << msg << std::endl; } } while(0)
#define LOG3(msg) do { if (LOG_LEVEL >= 3) { std::cout << msg << std::endl; } } while(0)

#include <sstream>
inline std::string shape1(int a) { std::ostringstream s; s<<a; return s.str(); }
inline std::string shape3(int a,int b,int c) {
    std::ostringstream s; s<<"["<<a<<"]["<<b<<"]["<<c<<"]"; return s.str(); }

inline int rom_log_interval(int total_floats) {
    int interval = total_floats / 10;
    if (interval <= 0) return total_floats + 1;
    int mag = 1;
    while (mag * 10 < interval) mag *= 10;
    return ((interval + mag - 1) / mag) * mag;
}

#endif // CONFIG_H
