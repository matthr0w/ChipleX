# LZ4 Compression/Decompression Example

## Overview

This example compresses and decompresses a test string.

## Setup & Usage

1. **Prepare the environment**  
   Copy the following files into the project:
   - Header files → `include/usercode/`
   - Source files → `include/usercode/`
   - Configuration → `system.yaml` (place it in the same directory as the executable)

   Add the following line to the `CMakeLists.txt`:
   ```cmake
   set(PROJECT_SOURCES ${PROJECT_SOURCES} ${PROJECT_SOURCE_DIR}/include/usercode/lz4.c)
   ```

2. **Build the project**  
   Compile the simulation:
   ```bash
   make
   ```

3. **Run the example**  
   The program will print the results to the terminal.

> The `spike` directory is not required and can be ignored.