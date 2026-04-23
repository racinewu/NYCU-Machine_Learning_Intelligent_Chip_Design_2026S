# Channels and Interfaces in SystemC
This project extends the AlexNet implementation by decomposing the network into distinct sub-modules and employing various SystemC Channels (sc_signal, sc_buffer, and sc_fifo) to establish communication. The goal is to explore hardware-oriented modeling techniques and analyze the impact of different communication protocols on simulation behavior and performance.

## Problem Formulation
The AlexNet architecture is partitioned into logical stages. The system must correctly transmit high-dimensional feature maps and flattened vectors between these modules using SystemC interfaces. Similar to HW1, the model loads pre-trained weights and processes a 224×224 RGB image, ensuring that the modularized forward pass yields identical results to the PyTorch reference for the top-100 ImageNet classes.

## Features
- **Three-Stage Modular Partitioning**:
  - Module A: Handles input padding, Conv1, ReLU, and Pool1.
  - Module B: Processes intermediate layers from Conv2 through Pool5.
  - Module C: Computes the final classifier stages (FC6 to Softmax).
- **Multi-Channel Communication**: Implements three separate versions to compare communication overhead:
  - sc_signal: Standard event-driven updates (triggered on value change).
  - sc_buffer: Force-triggered updates (triggered on every write, even for identical values).
  - sc_fifo: High-level abstract communication using blocking read/write and internal buffering.
- **Custom Data Wrappers**: Uses FeatureMapSignal and FlatSignal structs with overloaded operators to facilitate the transmission of complex STL-based data structures through SystemC channels.
- **Hierarchical Design**: Demonstrates the use of a Top module (AlexNet) to instantiate and interconnect sub-modules via port-to-channel binding.

## Processing Pipeline
1. **Weight Initialization**: The AlexNet top module triggers the load_weights() function for all sub-modules. Module A, Module B, and Module C load their respective pre-trained weights and biases from the data/ directory into their internal layer implementations.
2. **Input Generation (Pattern)**: The Pattern module reads the input image text file and provides raw RGB pixel data to the system. It asserts the in_valid signal to notify the downstream modules that a new inference cycle has begun.
3. **Front-end Processing (Module A)**: Module A receives the pixels from the Pattern module. It performs asymmetric zero-padding to reshape the input to 227×227 and executes the initial stages of the network (Conv1, ReLU, and Pool1). The resulting feature map is then transmitted through the selected SystemC channel.
4. **Mid-stream Feature Extraction (Module B)**: Module B captures the data from Module A. It processes the core convolutional backbone (Conv2 through Conv5 and their associated ReLU/Pooling layers). Once the deep features are extracted, it flattens the output into a 9,216-element vector for the classifier stage.
5. **Back-end Classification (Module C)**: Module C accepts the flattened vector and executes the final fully connected layers (FC6, FC7, and FC8). It applies the Softmax function to compute the probability distribution across 1,000 ImageNet classes.
6. **Result Verification (Pattern)**: The final logits and softmax probabilities are sent from Module C back to the Pattern module. The Pattern module then sorts the predictions, identifies the top-100 classes, and verifies the results against the reference model, concluding the simulation cycle.
7. **Channel Synchronization**: Throughout the pipeline, synchronization is governed by the channel implementation: sc_signal/sc_buffer versions utilize out_valid handshake signals, whereas the sc_fifo version leverages blocking read/write operations to manage the data flow between modules.

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
HW2/
  ├── code
  │   ├── 00_TESTBED
  │   │   ├── Makefile                  // Build script to compile the project
  │   │   ├── Pattern.cpp               // Loading input images and verifying classification results
  │   │   ├── Pattern.h
  │   │   └── data
  │   │       ├── cat.txt               // Cat image input
  │   │       ├── dog.txt               // Dog image input
  │   │       ├── conv#_bias.txt        // Conv layer biases (# = 1~5)
  │   │       ├── conv#_weight.txt      // Conv layer weights (# = 1~5)
  │   │       ├── fc#_bias.txt          // FC layer biases (# = 6~8)
  │   │       ├── fc#_weight.txt        // FC layer weights (# = 6~8)
  │   │       └── imagenet_classes.txt  // 1000 ImageNet class names
  │   │
  │   ├── 09_SUBMIT
  │   │   ├── 00_tar
  │   │   ├── 01_submit
  │   │   ├── 02_check
  │   │   └── demo_hw2.py
  │   │
  │   └── 01_SOURCE
  │       └── <channel>
  │           ├── result_cat_golden.log
  │           ├── result_dog_golden.log
  │           ├── run                   // Final executable, e.g., run
  │           │
  │           ├── alexnet_<channel>.cpp // Entry point; runs AlexNet forward pass
  │           ├── alexnet_<channel>.h   // Top-level module
  │           ├── moduleA.h             // Input, Conv1, and Pool1 layers
  │           ├── moduleB.h             // Conv2-5 and Pool5 layers
  │           ├── moduleC.h             // FC layers and Softmax output
  │           └── layers.h              // Layer computation logic and custom signal structures
  │
  ├── HW2.pdf
  └── README.md
```
## Usage Guide
> [!IMPORTANT]
> All source files must be placed within folders of `SC_SIGNAL|SC_BUFFER|SC_FIFO` for compilation, execution, and submission to work correctly.  
> In the official package, `Makefile` and `Pattern` are placed in `00_TESTBED` and already symlinked into each `01_SOURCE/<channel>` directory.  
> If you reconstruct the project manually, ensure these symbolic links are created accordingly.

To generate the executable `run`, simply run
```
cd 01_SOURCE/<channel>
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