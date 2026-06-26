#ifndef CONFIG_H
#define CONFIG_H

// ==================================================
// Architecture configuration
// ==================================================

// Number of cores for each phase (must divide output channels evenly)
#define CORES_CONV1   4    // Conv1: 64 output ch / 4 = 16 ch/core
#define CORES_CONV2  16    // Conv2: 192 / 16 = 12 ch/core
#define CORES_CONV3  16    // Conv3: 384 / 16 = 24 ch/core
#define CORES_CONV4  16    // Conv4: 256 / 16 = 16 ch/core
#define CORES_CONV5  16    // Conv5: 256 / 16 = 16 ch/core
#define CORES_FC6     8    // FC6:  4096 / 8 = 512 neurons/core
#define CORES_FC7     8    // FC7:  4096 / 8 = 512 neurons/core
#define CORE_FC8     15    // FC8+Softmax: single core (ID 15)

// ==================================================
// MAC units per PE
//   Baseline  : 1  MAC/PE  -> compute cycles = total_mac_ops / 1
//   Optimized : 16 MACs/PE -> compute cycles = ceil(total_mac_ops / 16)
// Clock period: 10 ns
// ==================================================
#define MAC_PER_PE    1     // Baseline: 1 MAC per PE
#define CLK_PERIOD_NS 10

// ==================================================
// Log level
//   0 = silent
//   1 = phase-level   (image read, conv1 start/done, fc6 done, ...)
//   2 = tile-level    (core X conv5 ch Y-Z done, ROM read progress)
//   3 = flit-level    (recv pkt type=X ch_start=Y tile=Z size=W)
// ==================================================
#define LOG_LEVEL 2

// ==================================================
// Log macros
// ==================================================
#include <iostream>
#include <iomanip>

#define LOG1(msg) do { if (LOG_LEVEL >= 1) { std::cout << msg << std::endl; } } while(0)
#define LOG2(msg) do { if (LOG_LEVEL >= 2) { std::cout << msg << std::endl; } } while(0)
#define LOG3(msg) do { if (LOG_LEVEL >= 3) { std::cout << msg << std::endl; } } while(0)

// ==================================================
// Shape formatting helper
// Usage: SHAPE3(227,227,3) -> "[227][227][3]"
//        SHAPE1(150528)    -> "150528"
// ==================================================
#include <sstream>
inline std::string shape1(int a) {
    std::ostringstream s; s << a; return s.str();
}
inline std::string shape2(int a, int b) {
    std::ostringstream s; s << "[" << a << "][" << b << "]"; return s.str();
}
inline std::string shape3(int a, int b, int c) {
    std::ostringstream s; s << "[" << a << "][" << b << "][" << c << "]"; return s.str();
}
inline std::string shape4(int a, int b, int c, int d) {
    std::ostringstream s; s << "[" << a << "][" << b << "][" << c << "][" << d << "]"; return s.str();
}

// ==================================================
// ROM read interval helper
// Returns how often to print a progress log (every N floats)
// so total prints <= 10
// ==================================================
inline int rom_log_interval(int total_floats) {
    // ceil to next round number so output is <= 10 lines
    int interval = total_floats / 10;
    if (interval <= 0) return total_floats + 1; // never print
    // round up to a power-of-10-friendly number
    int mag = 1;
    while (mag * 10 < interval) mag *= 10;
    return ((interval + mag - 1) / mag) * mag;
}

#endif // CONFIG_H