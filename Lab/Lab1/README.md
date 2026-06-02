# SystemC Implementation of Port-to-Port Communication
AXI-Stream is a unidirectional, point-to-point protocol from the AXI4 family designed for high-throughput data streaming. It simplifies bus communication by removing address signals, relying solely on a handshake mechanism where data transfer occurs only when both TVALID and TREADY are asserted simultaneously.

## Problem Formulation
Given a simulation time of 50 seconds, MODULE1 generates a timestamp string every 5 seconds and must successfully relay it through MODULE2 → MODULE3 → CHECKER, while a direct bypass channel validates correctness. MODULE3 is constrained to use `SC_METHOD` + `SC_HAS_PROCESS` instead of the usual `SC_THREAD` + `SC_CTOR`.

## Features
- **Mixed Process Types**: MODULE3 uses SC_METHOD + SC_HAS_PROCESS with sensitivity-list re-triggering — no loop, no wait() — while MODULE1/2 use SC_THREAD.
- **Pointer-Based Port Access**: All reads/writes go through dereferenced ports (p->read(), p->write()), never directly touching the channel.
- **Three-Stage Channel Binding**: Three sc_signal<string> channels (s, s2, s3) are declared in sc_main and bound to the corresponding module ports.

## Processing Pipeline
1. **MODULE1 writes**: Every 5 seconds, append current simulation time to string and write to channel s via port p. Also write directly to bypass channel s_checker.
2. **MODULE2 transfers**: Triggered by s value change, read from port p, write to channel s2 via port p2, then wait() for next trigger.
3. **MODULE3 transfers**: Triggered by s2 value change via SC_METHOD, read from port p2, write to channel s3 via port p3. Re-triggered automatically by sensitivity list.
4. **CHECKER validates**: Reads from s3 via port p3 and compares against golden data from bypass channel s_checker. Prints pass/fail for each transfer.


## Input / Output Format
### Output
Simulation log
```
0 s:module1 writes to channel, string =0
0 s:module2 reads from channel, string=0
0 s:module2 writes to channel, string=0
0 s:module3 reads from channel, string=0
0 s:module3 writes to channel, string=0
0 s:checker reads from channel, string=0
data is correct at time 0 s
5 s:module1 writes to channel, string =5
5 s:module2 reads from channel, string=5
5 s:module2 writes to channel, string=5
5 s:module3 reads from channel, string=5
5 s:module3 writes to channel, string=5
5 s:checker reads from channel, string=5
data is correct at time 5 s
10 s:module1 writes to channel, string =10
10 s:module2 reads from channel, string=10
10 s:module2 writes to channel, string=10
10 s:module3 reads from channel, string=10
10 s:module3 writes to channel, string=10
10 s:checker reads from channel, string=10
data is correct at time 10 s
...
```

## Directory Structure
```
Lab1/
  ├── code
  │   ├── 09_SUMIT
  │   │   ├── 00_tar       // Pack source files into .tar.gz
  │   │   ├── 01_sumit     // Submit and auto-verify on TA server
  │   │   ├── 02_check     // Confirm submission result
  │   │   └── demo_lab1.sh // TA-provided verification script
  │   │
  │   ├── run              // Final executable, e.g., run
  │   ├── Makefile         // Build script to compile the project
  │   ├── port2port.cpp    // Port-to-port communication
  │   └── port2port.log    // Simulation log
  │
  ├── Lab1.pdf             // spec
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