# Simulation Environment for Chiplet-Based Systems

This project provides a high-level simulation environment for chiplet-based systems using SystemC.

## SystemC Installation

### Prerequisites

**Fedora**

```bash
sudo dnf install clang cmake
```

### Installation Steps

1. **Clone the SystemC repository**  
   Clone the official SystemC repository from Accellera:

   ```bash
   git clone https://github.com/accellera-official/systemc.git
   ```

2. **Create a build directory**  
   Change into the SystemC directory and create a build subdirectory:

   ```bash
   cd systemc
   mkdir build
   cd build
   ```

3. **Generate the Makefiles using CMake**  
   Configure the build to install SystemC into a local directory (`../install`):

   ```bash
   cmake .. -DCMAKE_INSTALL_PREFIX=../install
   ```

4. **Compile the source**  
   Compile the project using all available CPU cores:

   ```bash
   make -j$(nproc)
   ```

5. **Install SystemC**  
   Install the compiled library files:

   ```bash
   make install
   ```

### Environment Variables

`~/.bashrc`

```bash
export SYSTEMC_PATH=/path/to/systemc/install
export LD_LIBRARY_PATH=$SYSTEMC_PATH/lib:$LD_LIBRARY_PATH
```

## Building

```bash
make
```

## Model Information

### Planned

- [x] AXI4-like system bus with bursts and congestion handling

- [x] Improved cache and RAM delay models

- [ ] Interconnect protocol extensions:

  - [x] DMA support
  - [x] Read/write to non-main memories
  - [ ] Smart protocol controller features

- [x] Expanded configuration options:
  
  - [x] Selectable chiplet components (e.g., pure memory)
  - [x] Flexible interconnect parameters between chiplets
  - [x] Custom connection topologies

- [ ] Streamlined user code workflow (#8)

- [ ] Hardware accelerator modules with local memory (#9)

- [ ] Improved statistics collection, processing, and visualization (#11)

- [ ] New applications using accelerators and multi-core


## RISC-V Cycle Estimation

### Prerequisites

**RISC-V GNU Compiler Toolchain** 

https://github.com/riscv-collab/riscv-gnu-toolchain

**RISC-V Proxy Kernel and Boot Loader**

https://github.com/riscv-software-src/riscv-pk

**Spike RISC-V ISA Simulator**

https://github.com/riscv-software-src/riscv-isa-sim

### Simulation Steps

1. **Modify the user code to measure clock cycles**  
   Insert code to read the cycle CSR via RDCYCLE before and after the workload to capture cycle counts:

   ```C
   #include <stdio.h>

   uint64_t read_cycles(void) {
      uint64_t cycles;
      asm volatile ("rdcycle %0" : "=r" (cycles));
      return cycles;
   }

   int main() {
      uint64_t start_cycles = read_cycles();

      // USERCODE //

      uint64_t end_cycles = read_cycles();

      printf("Cycles: %lu\n", end_cycles - start_cycles);

      return 0;
   }
   ```

2. **Compile the code using the RISC-V toolchain**

   ```bash
   riscv64-unknown-elf-gcc -o usercode usercode.c
   ```

3. **Run the compiled program on Spike simulator**

   ```bash
   spike pk usercode
   ```