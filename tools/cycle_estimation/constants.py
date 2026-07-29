import os
from pathlib import Path

# RISC-V Compiler
RISCV_COMPILER = "riscv64-unknown-elf-gcc"
RISCV_COMPILER_FLAGS = ["-O0", "-lc", "-lstdc++", "-lsupc++"]

# LLVM-MCA
LLVM_ANALYZER = "llvm-mca"
LLVM_ANALYZER_FLAGS = ["-mtriple=riscv64", "-mcpu=rocket-rv64"]

# Spike
SPIKE_SIMULATOR = "spike"
SPIKE_SIMULATOR_FLAGS = ["pk"]

# Requirements
REQUIRED_TOOLS = [RISCV_COMPILER, LLVM_ANALYZER, SPIKE_SIMULATOR]

# Paths
# The config, setup, and scratch build locations default to the repository
# layout (relative to the working directory) but can be overridden via the
# environment so the GUI can point them at its managed workspace.
SCRIPT_ROOT = Path(__file__).parent.absolute()
## Configs
CONFIGS_ROOT = Path(os.environ.get("CE_CONFIGS_DIR", "configs"))
ACCELERATOR_CONFIGS = CONFIGS_ROOT / "accelerators"
## Setups
SETUPS_ROOT = Path(os.environ.get("CE_SETUPS_DIR", "setups"))
SETUP_SRC_SUBDIR = Path("src")
SETUP_PROGRAM_FILE = SETUP_SRC_SUBDIR / "program.cpp"
SETUP_WORKLOADS_SUBDIR = Path("workloads")
SETUP_WORKLOADS_DB = "workloads.yaml"
SETUP_SYSTEM_FILE = "system.yaml"
## Setup to process (empty = all setups)
ONLY_SETUP = os.environ.get("CE_ONLY_SETUP", "")
## Build
BUILD_DIR = Path(os.environ.get("CE_BUILD_DIR", str(SCRIPT_ROOT / "build")))

# Annotations
BEGIN_CYCLE_ANNOT = "//@BEGIN_CYCLE_MEASURE"
END_CYCLE_ANNOT = "//@END_CYCLE_MEASURE"
BEGIN_SPEEDUP_ANNOT = "//@BEGIN_SPEEDUP_MEASURE"
END_SPEEDUP_ANNOT = "//@END_SPEEDUP_MEASURE"

# Macros
MEASURE_HEADER = "measure.h"
MEASURE_HEADER_CONTENT = r"""#pragma once
#include <cstdint>
#include <iostream>

#define BEGIN_CYCLE_MEASURE(SECTION)                                           \
  uint64_t __start_##SECTION;                                                  \
  asm volatile("rdcycle %0" : "=r"(__start_##SECTION));

#define END_CYCLE_MEASURE(SECTION)                                             \
  {                                                                            \
    uint64_t __end_##SECTION;                                                  \
    asm volatile("rdcycle %0" : "=r"(__end_##SECTION));                        \
    std::cout << #SECTION << ": " << (__end_##SECTION - __start_##SECTION)     \
              << " cycles" << std::endl;                                       \
  }

#define BEGIN_SPEEDUP_MEASURE(SECTION)                                         \
  asm volatile("# LLVM-MCA-BEGIN " #SECTION);

#define END_SPEEDUP_MEASURE(SECTION) asm volatile("# LLVM-MCA-END");
"""