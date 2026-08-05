from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict


@dataclass
class Workload:
    id: str
    file_id: str
    source_path: Path
    source_hash: str
    dest_path: Path
    asm_path: Path
    binary_path: Path
    estimation_result: int
    model: str = "riscv-minor"
    num_cycle_sections: int = 0
    has_speedup: bool = False

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