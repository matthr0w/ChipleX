"""A single simulator run: its parameters, sandbox, and command line.

The sandbox symlinks configs/ and the target setup's sibling files, and writes a
patched system.yaml carrying this run's overrides, so applying an override reuses
the simulator's own merge path.
"""

from __future__ import annotations

import copy
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, List, Tuple

import yaml

from .project import Project
from .system_model import ParamRef, apply_override, render


@dataclass
class RunSpec:
    label: str
    setup: str
    time_ns: float = 0.0  # 0 means run until the simulation ends on its own
    ber: float | None = None  # None keeps the simulator default
    seed: int | None = None
    overrides: List[Tuple[ParamRef, Any]] = field(default_factory=list)

    def argv(self, sim_binary: Path, stats_out: Path, log_level: str = "SILENT") -> List[str]:
        args = [
            str(sim_binary),
            f"--setup={self.setup}",
            f"--stats-out={stats_out}",
            f"--logging={log_level}",
        ]
        if self.time_ns and self.time_ns > 0:
            args.append(f"--time={self.time_ns}")
        if self.ber is not None:
            args.append(f"--ber={self.ber}")
        if self.seed is not None:
            args.append(f"--seed={self.seed}")
        return args

    def overrides_summary(self) -> str:
        parts = [f"{ref.label.split('.')[-1] if ref.special is None else ref.special}={value}"
                 for ref, value in self.overrides]
        return ", ".join(parts)

    def build_sandbox(self, project: Project, sandbox_dir: Path) -> Path:
        """Materialize the working directory the simulator runs in."""
        sandbox_dir.mkdir(parents=True, exist_ok=True)

        # Symlink targets are resolved to absolute paths so the sandbox works
        # regardless of the sandbox location or a relative setups/configs dir.
        configs_link = sandbox_dir / "configs"
        if not configs_link.exists():
            configs_link.symlink_to(project.configs_dir.resolve(), target_is_directory=True)

        setup_src = project.setup_dir(self.setup)
        setup_dst = sandbox_dir / "setups" / self.setup
        setup_dst.mkdir(parents=True, exist_ok=True)
        for entry in setup_src.iterdir():
            if entry.name == "system.yaml":
                continue
            link = setup_dst / entry.name
            if not link.exists():
                link.symlink_to(entry.resolve(), target_is_directory=entry.is_dir())

        doc = yaml.safe_load(setup_src.joinpath("system.yaml").read_text()) or {}
        doc = copy.deepcopy(doc)
        for ref, value in self.overrides:
            apply_override(doc, ref, value)
        (setup_dst / "system.yaml").write_text(render(doc))

        return sandbox_dir
