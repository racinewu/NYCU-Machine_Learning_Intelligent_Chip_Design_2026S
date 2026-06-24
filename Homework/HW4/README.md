# NoC-based CNN Architecture
This project implements a NoC-based AlexNet inference system using SystemC. Building on HW1–HW3, it integrates a partitioned AlexNet model onto a 4×4 mesh Network-on-Chip, where each core acts as a Processing Element (PE) executing a slice of the neural network. The Controller orchestrates all data movement via the NoC using a 4-phase handshake protocol, and all weights and input data are sourced exclusively through the ROM module.

## Problem Formulation
The goal is to build a complete NoC-based CNN accelerator that runs AlexNet inference end-to-end. Each PE should execute a partition of a neural network layer, the Controller should manage ROM access and coordinate all inter-PE data transfers over the NoC, and the final output must display the Top 100 ImageNet predictions with softmax probabilities in a specified format. Each case must complete simulation within one hour.

## Features
- **Channel-Parallel Execution**: Each layer is partitioned by output channel across multiple cores, so all assigned cores compute in parallel.
- **Dataflow Strategy**: Convolutional layers use Output Stationary (OS); FC6/FC7 use Weight Stationary (WS), where weights are preloaded once and only activations are broadcast per inference call.
- **Tiled NoC Transfers**: Large payloads are chunked into fixed-size tiles and reassembled at the destination, preventing single oversized packets.
- **Wide Flit Encoding**: Each data flit carries 4 floats (130-bit flit width), reducing the total flit count for large transfers.
- **Separated NI and PE Logic**: Each Core splits responsibilities into three threads — `rx_thread` (flit assembly), `compute_thread` (NN computation), and `tx_thread` (result sending) — keeping protocol handling decoupled from computation.

## Processing Pipeline
1. **Conv1–5**: For each layer, the Controller reads weights and biases from ROM, then sends each core its input feature map and assigned weight slice as a single packet. Cores execute conv + ReLU (+ max-pool where applicable) and return partial results; the Controller reassembles the full feature map before proceeding.
2. **FC Weight Preload**: After Conv5, the Controller distributes FC6 and FC7 weight partitions to cores via `PKT_FC_W`. Each core sends an acknowledgement back, ensuring weights are resident before activations arrive.
3. **FC6/FC7 Inference**: The Controller broadcasts the full activation vector to all FC cores. Each core computes its neuron partition with ReLU and returns partial outputs, which the Controller concatenates.
4. **FC8 + Softmax + Output**: The Controller sends FC8 weights and FC7 activations to a dedicated core, which returns raw logits and softmax probabilities. The Controller then prints the Top 100 classes sorted by probability.

## Parameters
- **FLOATS_PER_FLIT**: 4 floats per flit to reduce NoC traffic and simulation time during large weight transfers
- **TILE_SIZE**: 32,768 floats per NoC packet tile to limit packet size and prevent excessive software queue memory usage

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
HW4/
  ├── code
  │   ├── 09_SUBMIT
  │   │   ├── 00_tar
  │   │   ├── 01_submit
  │   │   ├── 02_check
  │   │   └── demo_hw4.py
  │   ├── data
  │   │   ├── cat.txt              // Cat image input
  │   │   ├── dog.txt              // Dog image input
  │   │   ├── conv#_bias.txt       // Conv layer biases (# = 1~5)
  │   │   ├── conv#_weight.txt     // Conv layer weights (# = 1~5)
  │   │   ├── fc#_bias.txt         // FC layer biases (# = 6~8)
  │   │   ├── fc#_weight.txt       // FC layer weights (# = 6~8)
  │   │   └── imagenet_classes.txt // 1000 ImageNet class names
  │   ├── result_cat_golden.log
  │   ├── result_dog_golden.log
  │   │
  │   ├── run                      // Final executable, e.g., run
  │   ├── Makefile                 // Build script to compile the project
  │   ├── main.cpp
  │   ├── clockreset.cpp           // Clock and reset generators
  │   ├── clockreset.h
  │   ├── ROM.cpp                  // TA-provided weight/bias/image reader
  │   ├── ROM.h
  │   ├── pe.h                     // Flit format, packet types, Packet struct
  │   ├── config.h                 // Core counts per layer, log level, shape helpers
  │   ├── core.h                   // NI (rx/tx) + PE (compute) per core
  │   ├── noc_io.h                 // NoC send/recv helpers
  │   ├── router.h                 // 5-stage XY router with CTRL_ID special routing
  │   └── controller.h             // Orchestrates inference
  ├── HW4.pdf
  └── README.md
```
> [!NOTE]
> Data folder are not included in this repository due to capacity limit.

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
make <cat|dog>
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