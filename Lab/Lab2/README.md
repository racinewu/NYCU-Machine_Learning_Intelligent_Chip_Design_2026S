# SystemC Implementation of AXI-Stream Forwarder
AXI-Stream is a unidirectional, point-to-point protocol from the AXI4 family designed for high-throughput data streaming. It simplifies bus communication by removing address signals, relying solely on a handshake mechanism where data transfer occurs only when both TVALID and TREADY are asserted simultaneously.

## Problem Formulation
Given a simulation time of 1000 ns, implement an AXI-Stream communication pipeline where a Producer sends three strings ("HELLO", "NYCU", "MLCHIP") character by character to a Checker for validation. The task is to complete the Forwarder module — a bridge between Producer and Checker — which must correctly relay AXI-Stream signals using `SC_METHOD` sensitive to `ACLK.pos()` and `ARESETn.neg()`.

## Features
- **AXI-Stream Handshake**: Data transfer occurs only when both TVALID and TREADY are asserted simultaneously, with TLAST marking the final character of each string.
- **Deadlock-Free Ready Signal**: `S_AXIS_TREADY` is asserted unconditionally in the else branch, independent of `S_AXIS_TVALID`, preventing producer stall.
- **Combinational Forwarding**: TDATA and TLAST are forwarded unconditionally every clock — safe under AXI-Stream since the Checker only reads on valid handshake.

## Processing Pipeline
1. **Producer sends**: Each rising clock edge, if TREADY is high, write current character to `M_AXIS_TDATA`, assert `M_AXIS_TVALID`, and set `M_AXIS_TLAST` on the last character of the string.
2. **Forwarder relays**: On reset, de-assert TVALID/TDATA/TLAST. Otherwise, assert `S_AXIS_TREADY`, and directly mirror `S_AXIS_TVALID`, `S_AXIS_TDATA`, `S_AXIS_TLAST` to the master interface.
3. **Checker validates**: On each valid handshake, append received character to buffer. On TLAST, compare full buffer against expected string and report correct/incorrect, then clear buffer for the next string.

## Input / Output Format
### Output
Simulation log
```
Simulation starting...
@ 1 ns Asserting reset (ARESETn = 0)
1 ns: Producer: Resetting...
1 ns: Checker: Resetting...
10 ns: Producer: Resetting...
10 ns: Checker: Resetting...
@ 11 ns De-asserting reset (ARESETn = 1)
30 ns: Producer: Sent 'H' (TLAST=0)
40 ns: Producer: Sent 'E' (TLAST=0)
50 ns: Producer: Sent 'L' (TLAST=0)
50 ns: Checker: Received 'H' (TLAST=0)
60 ns: Producer: Sent 'L' (TLAST=0)
...
```

## Directory Structure
```
Lab2/
  ├── code
  │   ├── 09_SUBMIT
  │   │   ├── 00_tar       // Pack source files into .tar.gz
  │   │   ├── 01_submit    // Submit and auto-verify on TA server
  │   │   ├── 02_check     // Confirm submission result
  │   │   └── demo_lab2.sh // TA-provided verification script
  │   │
  │   ├── run              // Final executable, e.g., run
  │   ├── Makefile         // Build script to compile the project
  │   ├── main.cpp
  │   ├── producer.h       // AXI-Stream master
  │   ├── checker.h        // AXI-Stream slave
  │   ├── forwarder.h      // AXI-Stream bridge
  │   └── forwarder.log    // Simulation log
  │
  ├── mlchip_lab2_AXI.pptx // spec
  └── README.md
```

## Usage Guide
> [!IMPORTANT]
> All source files must be placed at the same level as `09_SUBMIT/` for compilation, execution, and submission to work correctly.
### How to compile
To generate the executable `run`, simply run
```
make
```
### How to execute
Run the program with
```
./run
```
### How to submit
First, compress your code into a tar archive
```
cd 09_SUMIT
./00_tar
```
Then submit to the TA server
```
./01_submit
```
To verify your submission result
```
./02_check
```