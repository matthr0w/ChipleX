# Parallel Matrix Multiplication Example

## Overview

This example performs parallel matrix multiplication of two **16x16** matrices distributed across four chiplets, where each chiplet computes one matrix row.

## Setup & Usage

1. **Prepare the environment**  
   Copy the following files into the project:
   - Header files → `include/usercode/`
   - Configuration → `system.yaml` (place it in the same directory as the executable)

2. **Build the project**  
   Compile the simulation:
   ```bash
   make
   ```

3. **Run the example**  
   The program will print the results to the terminal.

> The `spike` directory is not required and can be ignored.