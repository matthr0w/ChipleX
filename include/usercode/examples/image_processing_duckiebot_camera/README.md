# Duckiebot Camera Example

## Description

This example processes the `duckiebot_input.jpg` image by cropping and converting it to grayscale, then saves the result as `duckiebot_outputX.jpg`. The operation repeats every **8 ms** for ten times to simulate a **120 FPS** video stream.

## Requirements

1. The example is designed for two chiplets.
2. The chiplets and the FPGA must have at least **5 KB** of RAM each.

## Usage

To use this example, copy the header files and the `duckiebot_input.jpg` image file into the `usercode` directory, then build the project. You can ignore the `spike` directory.