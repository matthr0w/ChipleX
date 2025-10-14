# TUM Logo Example

## Overview

This example reads the `tum_input.jpg` image, crops and inverts it, and finally saves it as `tum_output.jpg`.

## Setup & Usage

1. **Prepare the environment**  
   Copy the following files into the project:
   - Header files → `include/usercode/`
   - Input image → `include/usercode/tum_input.jpg`
   - Configuration → `system.yaml` (place it in the same directory as the executable)

2. **Build the project**  
   Compile the simulation:
   ```bash
   make
   ```

3. **Run the example**  
   The program will generate the cropped and grayscale frame.

> The `spike` directory is not required and can be ignored.