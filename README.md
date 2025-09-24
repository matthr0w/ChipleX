# Simulation Environment for Chiplet-Based Systems

This project provides a high-level simulation environment for chiplet-based systems using SystemC.

## SystemC Installation

### Prerequisites

**Fedora**

```bash
sudo dnf install clang cmake
sudo dnf install glibc-static libstdc++-static  # required for static linking
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
   Configure the build to install SystemC into a local directory (`../install`) and build using static libraries:

   ```bash
   cmake .. -DCMAKE_INSTALL_PREFIX=../install -DBUILD_SHARED_LIBS=OFF
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

- [ ] Hardware accelerator modules with local memory

- [ ] Expanded configuration options:
  
  - [ ] Selectable chiplet components (e.g., pure memory)
  - [ ] Flexible interconnect types between chiplets and FPGA
  - [ ] Custom connection topologies

- [ ] New user applications using accelerators and multi-core

- [ ] Improved statistics collection, processing, and visualization

- [ ] Streamlined user code workflow

- [ ] Graphical user interface

### Usage

The program provides a command-line interface that allows the user to configure the basic simulation setup. More advanced parameters can be modified in the configuration files (see [Configurations](#configurations)).

```bash
./sim [options]
Options:
   --time=<ns>               Set simulation time in nanoseconds (default: unlimited)
   --chiplets=<n>            Set number of chiplets (minimum: 2, default: 2)
   --connection-type=<type>  Set interconnect type: Custom, PCIe, UCIe, SPI (default: Custom)
   --connections=1,2,3       Set FPGA connection targets: 1,2,...,n (default: 1,2)
   --chiplet-distance=<um>   Set distance between chiplets in micrometers (default: 100)
   --fpga-distance=<mm>      Set distance between FPGA and chiplets in millimeters (default: 5000)
   --ber=<prob>              Set bit error rate (default: 1e-12)
   --logging=level           Set logging level: INFO, WARN, ERROR, DEBUG, SILENT (default: ERROR)
   --help                    Show this help message
```

### Configurations

The model is highly configurable. All configuration files are located in the `configs` directory. The `Chiplet.yaml` and `FPGA.yaml` files contain basic parameters for the respective modules, while the `interconnects` subdirectory includes various configuration files for different interconnect types. These interconnect types can be selected via the command-line interface (see [Usage](#usage)).

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