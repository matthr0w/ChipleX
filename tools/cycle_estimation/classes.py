from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List


@dataclass
class Execution:
    chiplet: str
    module: str
    model: str = "riscv-minor"
    params: Dict = field(default_factory=dict)
    clock: str = "1ns"
    mem_latency: str = "3ns"
    input_hash: str = ""
    estimation_result: int = 0

    @property
    def executor(self) -> str:
        return f"{self.chiplet}.{self.module}"

@dataclass
class Workload:
    id: str
    file_id: str
    source_path: Path
    source_hash: str
    dest_path: Path
    asm_path: Path
    binary_path: Path
    num_cycle_sections: int = 0
    has_speedup: bool = False
    # One execution per (chiplet, module) that runs this region.
    executions: List[Execution] = field(default_factory=list)

@dataclass
class Setup:
    id: str
    path: Path
    program_file: Path
    system_file: Path
    workloads_db: Path
    workloads: Dict[str, Workload] = field(default_factory=dict)

    def __hash__(self):
        return hash(self.id)

    def __eq__(self, other):
        if not isinstance(other, Setup):
            return False
        return self.id == other.id
