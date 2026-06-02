# DMA
Direct Memory Access (DMA) is a mechanism that allows peripherals to transfer data independently without continuous CPU intervention. By issuing read address requests and receiving data autonomously, a DMA master can efficiently stream data through a pipeline, decoupling data production from consumption.

## Problem Formulation
Given a simulation time of 1000 ns, implement an AXI-Stream DMA read pipeline where a Producer holds a string of "MLCHIP" and sends only upon request. The task is to complete the Forwarder module — a DMA master between Producer and Checker — which must actively issue read address requests and relay received AXI-Stream data using `SC_THREAD` sensitive to `ACLK.pos()`.

## Features
- **AXI-Stream Handshake**: Data transfer occurs only when both TVALID and TREADY are asserted simultaneously, with TLAST marking the final character of each string.
- **Active Read Address Request**: Forwarder initiates data transfer by asserting `REQ_VALID_TO_PROD` and waiting for `REQ_READY_FROM_PROD`, completing the address handshake before any data flows.
- **Deadlock-Free Ready Signal**: `S_AXIS_TREADY` is asserted unconditionally, independent of `S_AXIS_TVALID`, preventing producer stall.
- **Sequential DMA Scheduling**: Using `SC_THREAD`, the Forwarder executes a deterministic sequence — request, receive, forward — repeating for each string, which cannot be expressed with a stateless `SC_METHOD`.

## Processing Pipeline
1. **Initialization**: On startup, de-assert all output signals and wait two clock edges for reset to take effect, then poll until `ARESETn` is high.
2. **Forwarder Isues Request**: Assert `REQ_VALID_TO_PROD` with `REQ_ID_TO_PROD=0`. Wait for `REQ_READY_FROM_PROD` handshake, then de-assert `REQ_VALID_TO_PROD` on the next cycle.
3. **Forwarder Relays Data**: Wait for `S_AXIS_TVALID`, then mirror `S_AXIS_TDATA` and `S_AXIS_TLAST` to the master interface with `M_AXIS_TVALID=1`. Wait for `M_AXIS_TREADY` handshake, advance one cycle, then de-assert `M_AXIS_TVALID`. Repeat until `M_AXIS_TLAST` is observed.
4. **Checker Validates**: On each valid handshake, append received character to buffer. On TLAST, compare full buffer against expected string and report correct/incorrect, then clear buffer for the next string.

## Input / Output Format

### Output
Simulation log
```
Simulation starting...
@ 1 ns Asserting reset (ARESETn = 0)
@ 11 ns De-asserting reset (ARESETn = 1)
30 ns: Producer: Accepted request for string ID 0. Becoming busy.
40 ns: Producer: Driving data 'M' (TLAST=0) for ID 0
40 ns: Producer: Data 'M' sent (handshake).
...
```

## Directory Structure
```
Lab3/
  ├── code
  │   ├── 09_SUBMIT
  │   │   ├── 00_tar       // Pack source files into .tar.gz
  │   │   ├── 01_submit    // Submit and auto-verify on TA server
  │   │   ├── 02_check     // Confirm submission result
  │   │   └── demo_lab3.sh // TA-provided verification script
  │   │
  │   ├── run              // Final executable, e.g., run
  │   ├── Makefile         // Build script to compile the project
  │   ├── main.cpp
  │   ├── producer.h       // AXI-Stream master
  │   ├── checker.h        // AXI-Stream slave
  │   ├── forwarder.h      // AXI-Stream bridge
  │   └── result_run.log   // Simulation log
  │
  ├── mlchip_lab3_DMA.pptx // spec
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