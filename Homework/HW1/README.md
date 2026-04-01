# SystemC Implementation of AlexNet
AlexNet is a deep convolutional neural network originally designed for large-scale image classification. This project implements the full forward pass of AlexNet using SystemC, simulating the computation of each layer in software to verify correctness against a pre-trained PyTorch model.

## Problem Formulation
Given a 224×224 RGB image and a set of pre-trained weights exported from PyTorch, the network must perform a full forward pass through five convolutional layers, three max-pooling layers, and three fully connected layers, followed by a Softmax layer. The output is a probability distribution over 1000 ImageNet classes, and the top-100 predictions must be displayed with their corresponding logit values and probabilities.

> [!NOTE]
> Use AlexNet model parameters from the information table. Ignore incorrect values in the example figure.

## Features
- **Full AlexNet Architecture**: Implements all layers of the original AlexNet including Conv, MaxPool, FC, and Softmax.
- **Modular SC_MODULE Design**: Each layer is encapsulated as an independent SystemC module for clarity and reusability.
- **Pointer-Based Data Passing**: Layer outputs are passed by pointer to the next layer, avoiding unnecessary vector copies and reducing memory overhead.
- **Unified Data Loading**: A single load_txt() utility function handles all weight and image file reading, making it easy to add new layers with minimal code.

## Processing Pipeline
1. **Load weights**: Read all convolutional and fully connected layer weights and biases from text files in data/ into each layer module.
2. **Load and pad input**: Read the input image text file (channel-first raster scan), apply asymmetric zero padding (top/left +2, bottom/right +1) to convert 224×224 to 227×227.
3. **Forward pass**: Sequentially call each layer's process(). Each layer receives a pointer to the previous layer's output vector, computes its result, and stores it in its own output buffer.
4. **Flatten**: After Pool5, reshape the 3D feature map (256×6×6) into a 1D vector of size 9216 to feed into the fully connected layers.
5. **Classification**: FC8 produces 1000 raw logits, which are passed to the Softmax layer to convert into a probability distribution.
6. **Output**: Monitor module reads the logits and probabilities, loads imagenet_classes.txt, sorts all 1000 classes by probability, and prints the Top-100 results in the required format.


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
HW1/
  ├── code
  │   ├── 09_SUMIT
  │   │   ├── 00_tar               // Pack source files into .tar.gz
  │   │   ├── 01_sumit             // Submit and auto-verify on TA server
  │   │   ├── 02_check             // Confirm submission result
  │   │   └── demo_hw1.py          // TA-provided verification script
  │   ├── data
  │   │   ├── cat.txt              // Cat image input
  │   │   ├── dog.txt              // Dog image input
  │   │   ├── conv#_bias.txt       // Conv layer biases (# = 1~5)
  │   │   ├── conv#_weight.txt     // Conv layer weights (# = 1~5)
  │   │   ├── fc#_bias.txt         // FC layer biases (# = 6~8)
  │   │   ├── fc#_weight.txt       // FC layer weights (# = 6~8)
  │   │   └── imagenet_classes.txt // 1000 ImageNet class names
  │   ├── other
  │   │   ├── cat.jpg
  │   │   └── dog.jpg
  │   │
  │   ├── run                      // Final executable, e.g., run
  │   ├── Makefile                 // Build script to compile the project
  │   ├── main.cpp                 // Entry point; runs AlexNet forward pass
  │   ├── alexnet.cpp              // Top-level AlexNet module
  │   ├── alexnet.h
  │   ├── layers.h                 // All layer SC_MODULE definitions
  │   └── monitor.h                // Output formatting and Top-100 display
  │
  ├── HW1.pdf
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
