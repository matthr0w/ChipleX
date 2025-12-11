#!/usr/bin/env python3
import hashlib
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from logging import log_error, log_info, log_warn
from pathlib import Path
from typing import Dict, List

import yaml

WORKLOADS_SUBDIR = "workloads"
WORKLOADS_YAML = "workloads.yaml"
COMPILE_CMD = ["riscv64-unknown-elf-gcc"]
COMPILE_FLAGS = ["-lc", "-lstdc++", "-lsupc++"]
START_ANNOT = "//@START_MEASURE"
END_ANNOT = "//@END_MEASURE"
MEASURE_HEADER = "measure_macros.h"
MEASURE_HEADER_CONTENT = r"""#pragma once
#include <cstdint>
#include <iostream>

#define BEGIN_CYCLE_MEASURE(name)                                              \
  uint64_t __start_##name;                                                     \
  asm volatile("rdcycle %0" : "=r"(__start_##name));

#define END_CYCLE_MEASURE(name)                                                \
  {                                                                            \
    uint64_t __end_##name;                                                     \
    asm volatile("rdcycle %0" : "=r"(__end_##name));                           \
    std::cout << #name << ": " << (__end_##name - __start_##name) << " cycles" \
              << std::endl;                                                    \
  }
"""

@dataclass
class WorkloadEntry:
    filepath: Path
    copypath: Path
    stem: str
    source_hash: str

@dataclass
class Setup:
    path: Path
    name: str
    workloads_yaml: Path
    workloads: Dict[str, WorkloadEntry] = field(default_factory=dict)

class CycleManager:
    def __init__(self, setups_root: Path, tmp_root: Path):
        self.setups_root = Path(setups_root)
        self.tmp_root = Path(tmp_root)
        self.setups: List[Setup] = []

    @staticmethod
    def sha1_file(path: Path) -> str:
        h = hashlib.sha1()
        with path.open("rb") as f:
            while True:
                chunk = f.read(8192)
                if not chunk:
                    break
                h.update(chunk)
        return h.hexdigest()

    @staticmethod
    def load_yaml(path: Path) -> dict:
        if not path.exists():
            return {}
        with path.open("r", encoding="utf-8") as f:
            return yaml.safe_load(f) or {}

    @staticmethod
    def write_yaml(path: Path, data: dict):
        with path.open("w", encoding="utf-8") as f:
            yaml.safe_dump(data, f, sort_keys=False)

    def update_yaml(self, setup: Setup, workload: WorkloadEntry, value: int):
        yaml_data = self.load_yaml(setup.workloads_yaml)
        if "workloads" not in yaml_data:
            yaml_data["workloads"] = {}
        # Write/update YAML
        yaml_data["workloads"].setdefault(workload.stem, {})
        yaml_data["workloads"][workload.stem]["cycles_count"] = value
        yaml_data["workloads"][workload.stem]["source_hash"] = workload.source_hash
        # Write YAML back
        self.write_yaml(setup.workloads_yaml, yaml_data)

    def scan_setups(self):
        for setup_dir in sorted(self.setups_root.iterdir()):
            if not setup_dir.is_dir():
                continue
            workloads_dir = setup_dir / WORKLOADS_SUBDIR
            if not workloads_dir.exists():
                continue
            name = setup_dir.name
            workloads_yaml = setup_dir / WORKLOADS_YAML
            setup = Setup(path=setup_dir, name=name, workloads_yaml=workloads_yaml)
            for cpp_file in sorted(workloads_dir.glob("*.cpp")):
                stem = cpp_file.stem
                sha = self.sha1_file(cpp_file)
                entry = WorkloadEntry(filepath=cpp_file, copypath=None, stem=stem, source_hash=sha)
                setup.workloads[stem] = entry
            self.setups.append(setup)

    def determine_updates(self) -> Dict[Path, List[WorkloadEntry]]:
        updates: Dict[Path, List[WorkloadEntry]] = {}
        for setup in self.setups:
            yaml_data = self.load_yaml(setup.workloads_yaml)
            workloads_yaml = yaml_data.get("workloads", {}) if yaml_data else {}
            pending = []
            for stem, entry in setup.workloads.items():
                # Check if entry exists
                yaml_entry = workloads_yaml.get(stem)
                if yaml_entry is None:
                    pending.append(entry)
                    continue
                # Check if hash exists and is valid
                yaml_hash = yaml_entry.get("source_hash")
                if not yaml_hash or yaml_hash != entry.source_hash:
                    pending.append(entry)
                    continue
                # Check if cycles count exists
                yaml_cycles = yaml_entry.get("cycles_count")
                if yaml_cycles is None or not isinstance(yaml_cycles, int):
                    pending.append(entry)
                    continue
            if pending:
                updates[setup.path] = pending
        return updates

    def prepare_tmp(self, setup: Setup, workload: WorkloadEntry):
        # Create tmp directory
        if self.tmp_root.exists():
            shutil.rmtree(self.tmp_root)
        self.tmp_root.mkdir(parents=True, exist_ok=True)
        # Copy workload .cpp file
        dest = self.tmp_root / f"{setup.name}__{workload.stem}.cpp"
        shutil.copy2(workload.filepath, dest)
        workload.copypath = dest
        # Copy headers and sources (except program.h/program.cpp)
        include_dir = setup.path / "include"
        src_dir = setup.path / "src"
        if include_dir.exists():
            for hdr in include_dir.glob("*.h"):
                if hdr.name == "program.h":
                    continue
                dest = self.tmp_root / hdr.name
                shutil.copy2(hdr, dest)
        if src_dir.exists():
            for src in src_dir.glob("*.*"):
                if src.suffix in {".c", ".cpp", ".cc", ".C"} and src.name != "program.cpp":
                    dest = self.tmp_root / src.name
                    shutil.copy2(src, dest)
        # Create measure macros header
        macros_header = self.tmp_root / MEASURE_HEADER
        macros_header.write_text(MEASURE_HEADER_CONTENT, encoding="utf-8")

    def prepare_workload(self, workload: WorkloadEntry):
        text = workload.copypath.read_text(encoding="utf-8")
        # Check if main function exists
        if not re.search(r"\bint\s+main\s*\(", text):
            log_error(f"{workload.filepath}: does not contain 'main()' definition. Workloads must define 'main()'.")
        # Check annotation counts
        start_count = text.count(START_ANNOT)
        end_count = text.count(END_ANNOT)
        if start_count != 1 or end_count != 1:
            log_error(f"{workload.filepath}: does not contain '{START_ANNOT}' and '{END_ANNOT}' annotations exactly once.")
        # Create safe identifier for C
        safe_identifier = re.sub(r"\W", "_", workload.stem)  # only word chars and underscore
        text = text.replace(START_ANNOT, f"BEGIN_CYCLE_MEASURE({safe_identifier})")
        text = text.replace(END_ANNOT, f"END_CYCLE_MEASURE({safe_identifier})")
        # Add include line if not present
        if MEASURE_HEADER not in text:
            text = f'#include "{MEASURE_HEADER}"' + "\n" + text
        # Write back
        workload.copypath.write_text(text, encoding="utf-8")

    def compile_tmp(self) -> Path:
        # Find all .c and .cpp in tmp
        files = []
        for ext in ("*.cpp", "*.c"):
            files.extend([str(p) for p in self.tmp_root.glob(ext)])
        # Compile binary for Spike
        cmd = COMPILE_CMD + files + ["-I.", "-o", "cycle_estimation"] + COMPILE_FLAGS
        proc = subprocess.run(cmd, cwd=str(self.tmp_root), stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if proc.returncode != 0:
            log_error(f"Compilation failed:\n{proc.stderr}")
        return self.tmp_root / "cycle_estimation"

    def parse_spike(self, binary_path: Path) -> int:
        cmd = ["spike", "pk", str(binary_path)]
        proc = subprocess.run(cmd, cwd=str(self.tmp_root), stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        output = (proc.stdout or "") + "\n" + (proc.stderr or "")
        if proc.returncode != 0:
            log_error(f"Spike simulation failed:\n{proc.stderr}")
        # Parse lines like: <identifier>: <N> cycles
        pattern = re.compile(r'^\s*[A-Za-z_]\w*\s*:\s*(\d+)\s*cycles\s*$', re.MULTILINE)
        match = pattern.search(output)
        if match:
            return int(match.group(1))
        # Store full output to help debugging
        debug_out = self.tmp_root / "spike_output.txt"
        debug_out.write_text(output, encoding="utf-8")
        log_error(f"No measurements parsed from Spike output. See {debug_out}.")
        return None

    def run(self):
        self.scan_setups()
        updates = self.determine_updates()
        if not updates:
            log_info("All workloads up-to-date.")
            return
        for setup_path, workloads in updates.items():
            setup = next(s for s in self.setups if s.path == setup_path)
            for workload in workloads:
                log_info(f"Updating cycle estimation for '{workload.filepath}'...")
                self.prepare_tmp(setup, workload)
                self.prepare_workload(workload)
                binary = self.compile_tmp()
                value = self.parse_spike(binary)
                self.update_yaml(setup, workload, value)

def main():
    for tool in ["riscv64-unknown-elf-gcc", "spike"]:
        if shutil.which(tool) is None:
            log_warn(f"Required RISC-V tool '{tool}' not found in PATH. Skipping cycle estimation.")
            sys.exit(0)

    mgr = CycleManager(setups_root=Path("setups"), tmp_root=Path(__file__).parent.absolute() / "tmp")
    try:
        mgr.run()
    except Exception as e:
        log_error(e)

if __name__ == "__main__":
    main()