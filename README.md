# Simulation Environment for Chiplet-Based Systems

This project provides a high-level simulation environment for chiplet-based systems using SystemC.

## SystemC Installation

SystemC serves as the foundation of this simulation environment, and its installation is therefore mandatory. Please follow the steps below to set it up correctly.

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

## RISC-V Toolchain & Spike

The RISC-V toolchain and the Spike simulator are used to estimate the execution cycle counts of the setup programs. While their installation is optional, it is highly recommended in order to obtain realistic processing delay estimations. Please refer to the links listed below for installation instructions.

**RISC-V GNU Compiler Toolchain** 

https://github.com/riscv-collab/riscv-gnu-toolchain

**RISC-V Proxy Kernel and Boot Loader**

https://github.com/riscv-software-src/riscv-pk

**Spike RISC-V ISA Simulator**

https://github.com/riscv-software-src/riscv-isa-sim

## Usage

### Build

```bash
make
```

### Execute Simulation

```bash
make run ARGS="--help"
```

## TODO

- [x] Advanced estimation of processing time and acceleration speedup factor

- [ ] Smart Chiplet Interconnect features

- [ ] Additional applications using multi-core and accelerators