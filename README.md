# Simulation Environment for Chiplet-Based Systems

This project provides a high-level simulation environment for chiplet-based systems using SystemC.

There are two ways to use it:

- **[Download the prebuilt application](#download-recommended)** - a single Linux
  executable with the GUI, the simulator, and SystemC bundled in. No SystemC
  installation required. Recommended for most users.
- **[Build from source](#build-from-source)** - for development; requires a local
  SystemC installation.

## Documentation

- [docs/GUI.md](docs/GUI.md) - using the GUI.
- [docs/PROGRAM-CODE.md](docs/PROGRAM-CODE.md) - writing setup program code (AXI/DMA APIs, cycle estimation).
- [docs/RELEASE.md](docs/RELEASE.md) - building a release bundle.

## Download (recommended)

Download the latest `chiplet-sim` from the project's **Releases** page (or the
CI pipeline artifacts), then run it:

```bash
chmod +x chiplet-sim
./chiplet-sim
```

The bundle is a self-contained Linux x86_64 executable. It runs the included
setups out of the box. On first launch it seeds an editable workspace under
`~/.local/share/chiplet-sim/`.

Two optional capabilities depend on tools installed on your machine:

- **Building new or edited setups** requires a C++ compiler and `cmake`.
- **Cycle estimation** requires LLVM, the RISC-V toolchain, and Spike (see below); without them,
  setups run with their existing workload cycle counts.

## Build from source

### Prerequisites

Install a C++17 compiler, CMake, and Git, for example:

```bash
sudo apt install build-essential cmake git  # Debian/Ubuntu
sudo dnf install gcc-c++ cmake git          # Fedora
```

### SystemC

SystemC is handled automatically: on the first build, the Makefile fetches and
builds a local SystemC 3.0.x into `.systemc-install/` and uses it thereafter.
To use an existing SystemC instead, set
`SYSTEMC_PATH` to its install prefix (containing `include/` and
`lib/libsystemc.so`).

## LLVM, RISC-V Toolchain & Spike

LLVM, the RISC-V toolchain and the Spike simulator are used to estimate the execution cycle counts of the setup programs. While their installation is optional, it is highly recommended in order to obtain realistic processing delay estimations. Please refer to the links listed below for installation instructions.

Cycle estimation runs automatically: the CLI runs it before every `make run`, and the GUI runs it when you build a setup. If the toolchain is not installed it is skipped, and setups run with their existing workload cycle counts.

**LLVM**

https://llvm.org

**RISC-V GNU Compiler Toolchain** 

https://github.com/riscv-collab/riscv-gnu-toolchain

**RISC-V Proxy Kernel and Boot Loader**

https://github.com/riscv-software-src/riscv-pk

**Spike RISC-V ISA Simulator**

https://github.com/riscv-software-src/riscv-isa-sim

## Usage

### Build

```bash
make [release|debug|asan]
```

The first build also compiles SystemC; later builds reuse it.

### Execute Simulation GUI *(recommended)*

```bash
make gui
```

### Execute Simulation CLI

```bash
make run ARGS="--help"
```

### Package a release

Build the self-contained bundle described in [Download](#download-recommended):

```bash
make bundle  # produces dist/chiplet-sim
```

See [docs/RELEASE.md](docs/RELEASE.md) for details and CI.

### Testing

Run every setup and compare its `stats.json` against the committed golden output in
`tests/golden/`:

```bash
make test
```

After an intentional, reviewed behavior change, regenerate the golden files with
`make test-update`.