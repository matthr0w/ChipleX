import os
from pathlib import Path

# RISC-V Compiler
RISCV_COMPILER = "riscv64-unknown-elf-g++"
RISCV_COMPILER_FLAGS = ["-O2"]

# LLVM-MCA
LLVM_ANALYZER = "llvm-mca"
LLVM_ANALYZER_FLAGS = ["-mtriple=riscv64", "-mcpu=rocket-rv64"]

# gem5 binary (GEM5_BIN) and source tree (GEM5_HOME, for the m5op header/shim;
# derived from the binary when unset).
GEM5_BINARY = os.environ.get("GEM5_BIN", "gem5.opt")
GEM5_HOME = os.environ.get("GEM5_HOME", "")

# Paths
# Setup/config/build locations default to the repo layout and can be
# overridden via the environment so the GUI can use its managed workspace.
SCRIPT_ROOT = Path(__file__).parent.absolute()
GEM5_DIR = SCRIPT_ROOT / "gem5"
GEM5_MODELS_DIR = GEM5_DIR / "models"
DEFAULT_MODEL = "riscv-minor"
DEFAULT_CLK_CYCLE_NS = 1
DEFAULT_ACCESS_LATENCY_CYCLES = 1
DEFAULT_MEM_CLK_CYCLE_NS = 3
## Configs
CONFIGS_ROOT = Path(os.environ.get("CE_CONFIGS_DIR", "configs"))
ACCELERATOR_CONFIGS = CONFIGS_ROOT / "accelerators"
CHIPLET_CONFIGS = CONFIGS_ROOT / "chiplets"
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
#include <gem5/m5ops.h>

#define BEGIN_CYCLE_MEASURE(SECTION) m5_reset_stats(0, 0);
#define END_CYCLE_MEASURE(SECTION) m5_dump_stats(0, 0);

#define BEGIN_SPEEDUP_MEASURE(SECTION)                                         \
  asm volatile("# LLVM-MCA-BEGIN " #SECTION);

#define END_SPEEDUP_MEASURE(SECTION) asm volatile("# LLVM-MCA-END");
"""
