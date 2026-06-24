# NoC-based CNN Accelerator System with AXI4 DMA Architecture & Design Optimization
SystemC is a C++-based hardware description language that enables behavioral simulation of digital systems at a high level of abstraction, allowing designers to verify functional correctness and evaluate performance trade-offs — including execution cycles, memory access volume, and resource utilization — before committing to RTL implementation. This project uses SystemC to simulate a Network-on-Chip (NoC) based CNN accelerator running AlexNet inference on a 4×4 mesh with 16 processing cores, providing early insight into the design space of compute resources, memory hierarchy, and data movement efficiency.

## Problem Formulation
The project requires building a NoC-based CNN accelerator system in SystemC that replaces ROM-based data storage with an external DRAM model accessible only through an AXI4-based DMA. The system must include a DRAM model with a clearly defined memory map, an AXI4 DMA with five independent channels supporting burst transfers and VALID/READY handshake, a PE array of at least 4×4 cores connected by a mesh NoC, and a controller coordinating data movement and computation. Both `make cat` and `make dog` must produce exactly correct top-100 classification results.
 
In addition to the baseline implementation, an optimized version must be proposed and compared against the baseline. The optimization is not limited to reducing execution cycles; it may target any aspect of the design including memory hierarchy, dataflow, NoC communication, or compute resources, with clear trade-off analysis presented in the report.

## Features
- **4×4 Mesh NoC**: 16 routers with XY routing, deadlock-free deterministic paths, and 5-stage pipeline (SyncIn -> ElasticBuffer -> RouteComputation -> Arbiter/Crossbar -> SyncOut)
- **AXI4 DMA**: 5-channel protocol (AR/R/AW/W/B) with VALID/READY handshake, INCR burst, and ARLEN/AWLEN/ARSIZE/AWSIZE signal support; dual master ports in optimized design for concurrent DMA access
- **Three-level memory hierarchy**:
  - *DRAM* (352 MB): external storage for all AlexNet weights, biases, image, intermediate data, and output
  - *On-chip SRAM*: shared staging buffer between DMA and PE array; single-bank 128 KB in baseline, 4-bank 512 KB with dedicated roles per bank in optimized
  - *PE local SRAM* (optimized only): 16 KB per PE as partial sum buffer for output stationary Conv dataflow
- **MAC Delay Model**: compute cycles estimated as `ceil(MAC_ops / MAC_PER_PE)` and injected via `wait()` for realistic cycle-count simulation
- **OS (Conv) + WS (FC) Dataflow**: selected for single-inference efficiency — OS reduces NoC round-trips for Conv, WS minimizes FC communication volume
- **FC Weight Tile Ping-Pong** (optimized only): weight tiled to bank size, DMA prefetch and PE compute overlapped via dual AXI ports
- **Execution Metrics**: per-layer cycle breakdown, PE utilization, DRAM/SRAM/AXI/NoC statistics

## Processing Pipeline
1. **DRAM Initialization**: All weights, biases, and the input image are loaded from the data folder into DRAM before simulation starts; all subsequent accesses go through the AXI DMA.
2. **Image Load**: Controller issues an AXI burst read to transfer the input image from DRAM through the SRAM Bank 0 staging buffer.
3. **Conv1–5 Execution**: For each layer, weights are DMA-loaded into the SRAM weight buffer (Bank 3), then the controller broadcasts the input FM, weight slice, and bias to assigned cores via NoC. Each core executes convolution and pooling under output stationary dataflow, accumulating partial sums in PE local SRAM (optimized only), and returns results to the controller.
4. **Feature Map Management**: Output FMs exceeding SRAM capacity are spilled to DRAM (baseline); the 512 KB SRAM in the optimized design eliminates all spills.
5. **FC6–7 Execution**: Weights are loaded and distributed to cores via PKT_FC_W under weight stationary dataflow; the small input activation vector is then broadcast. In the optimized design, Ping-Pong DMA overlaps weight tile loading with PE computation.
6. **FC8 + Softmax**: Executed on Core 15, producing 1000-class linear output and softmax probabilities.
7. **Output Write-back**: Results (2,000 floats) are written back to the DRAM output region via AXI DMA before being read back and printed as top-100 classification results.

## Parameters
### FP (Baseline)
- **PE array**: 16 PEs (4×4 mesh), 1 MAC unit per PE, 16 MACs total
- **PE local SRAM**: None
- **On-chip SRAM**: 1 bank × 128 KB = 128 KB total, 32-bit data width, 1-cycle read/write latency
- **DRAM**: 352 MB flat array, 32-bit per AXI beat, sequential burst access
- **AXI DMA**: Single master port, 5 independent channels, INCR burst, no outstanding transactions
- **CORES_CONV1**: 4 / **CORES_CONV2–5**: 16 / **CORES_FC6–7**: 8 / **CORE_FC8**: 1 (Core 15)
- **Clock period**: 10 ns
- **Tile size**: 32,768 floats per NoC packet tile

### FP_Optimized
- **PE array**: 16 PEs (4×4 mesh), 16 MAC units per PE, 256 MACs total
- **PE local SRAM**: 16 KB per PE (4,096 floats), used as partial sum buffer for Conv2–5 OS dataflow
- **On-chip SRAM**: 4 banks × 128 KB = 512 KB total, 32-bit data width, independent read/write per bank
  - Bank 0 (INPUT\_PING): input FM streaming buffer (Conv) / input activation lock (FC)
  - Bank 1 (INPUT\_PONG): unused in Conv / weight tile Ping — PE reads (FC)
  - Bank 2 (OUTPUT): unused in Conv / weight tile Pong — DMA prefetch (FC)
  - Bank 3 (WEIGHT): weight tile DMA staging (Conv) / bias buffer (FC)
- **DRAM**: 352 MB flat array, 32-bit per AXI beat
- **AXI DMA**: Dual master ports (Port 0: main thread, Port 1: prefetch thread), 5 independent channels per port, INCR burst, no outstanding transactions
- **CORES_CONV1**: 4 / **CORES_CONV2–5**: 16 / **CORES_FC6–7**: 16 / **CORE_FC8**: 1 (Core 15)
- **Clock period**: 10 ns
- **Tile size**: 32,768 floats per NoC packet tile / per SRAM bank (FC weight tiling)

## Input / Output Format
### Input
Each file stores values in plain text, one value per line, rounded to 6 decimal places.
Layout follows PyTorch's default order: `[out_ch][in_ch][kH][kW]` for weights, `[out_ch]` for biases.

### Output
Cat
```
Top 100 classes:
=================================================
  idx |      val | possibility | class name
-------------------------------------------------
  285 |    20.21 |       96.38 | Egyptian cat
  281 |    16.14 |        1.65 | tabby
  282 |    15.73 |        1.10 | tiger cat
  287 |    14.79 |        0.43 | lynx
  728 |    14.41 |        0.29 | plastic bag
...
```
Dog
```
=================================================
  idx |      val | possibility | class name
-------------------------------------------------
  207 |    16.59 |       38.63 | golden retriever
  175 |    15.57 |       13.86 | otterhound
  220 |    15.36 |       11.26 | Sussex spaniel
  163 |    15.00 |        7.86 | bloodhound
  219 |    14.59 |        5.22 | cocker spaniel
...
```

## Directory Structure
```
FP/
  ├── FP_Group
  │   └── <FP|FP_Optimized>
  │       ├── clockreset.cpp/.h // Clock and reset signal generation
  │       ├── config.h          // Architecture parameters and logging utilities
  │       ├── axi_dma.h         // AXI4 DMA master with 5-channel handshake protocol
  │       ├── controller.h      // Inference orchestrator managing DMA, NoC dispatch, and result collection
  │       ├── core.h            // Compute core integrating NI and PE with CNN layer execution logic
  │       ├── dram.h            // External DRAM behavior model with memory map definitions
  │       ├── sram.h            // On-chip SRAM behavior model with multi-bank support
  │       ├── pe.h              // Flit/packet definitions and NoC communication primitives
  │       ├── router.h          // 5-port mesh router with XY routing and pipeline stages
  │       ├── noc_io.h          // NoC send/receive helpers for tiled packet transfers
  │       ├── main.cpp          // Top-level module instantiation and NoC wiring
  │       ├── Makefile          // Build script to compile the project
  │       └── data/
  │
  ├── AXI_introduciton(1).pptx
  ├── mlchip_FP_2026.pdf
  ├── mlchip_FP_2026_v2.pdf
  └── README.md
```

### How to compile
To generate the executable `run`, simply run
```
make
```
### How to execute
Run the program with
```
make <cat|dog>
```

## Experiment
<p align="center">
  <img src="./Baseline.png" alt="Baseline Result" width="800">
</p>
<p align="center">Figure 1. Baseline result</p>

<p align="center">
  <img src="./Optimized.png" alt="Optimized Result" width="800">
</p>
<p align="center">Figure 2. Optimized result</p>