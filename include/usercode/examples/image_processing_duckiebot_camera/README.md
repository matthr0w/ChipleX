# Duckiebot Camera Example

## Overview

This example simulates a simple camera processing pipeline using the **Duckiebot** image.  
It reads the `duckiebot_input.jpg` image, crops it, converts it to grayscale, and saves the results as sequentially numbered output files (`duckiebot_outputX.jpg`).

The process repeats every **8 ms** (= 125 FPS) for **ten frames**, emulating a real-time video stream.

## Setup & Usage

1. **Prepare the environment**  
   Copy the following files into the project:
   - Header files → `include/usercode/`
   - Input image → `include/usercode/duckiebot_input.jpg`
   - Configuration → `system.yaml` (place it in the same directory as the executable)

2. **Build the project**  
   Compile the simulation:
   ```bash
   make
   ```

3. **Run the example**  
   The program will generate cropped and grayscale frames named:
   ```bash
   include/usercode/duckiebot_output0.jpg
   include/usercode/duckiebot_output1.jpg
   ...
   include/usercode/duckiebot_output9.jpg
   ```

> The `spike` directory is not required and can be ignored.