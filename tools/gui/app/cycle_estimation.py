"""Run the cycle-estimation tool for a setup, mirroring `make run`.

Estimation runs before every simulation so a run's workload cycle counts stay
consistent with the exact system.yaml the simulator loads. Because each run
estimates against its own sandbox (with that run's overrides applied), the
caller can point it at the sandbox's setups directory. It is skipped only in a
frozen bundle; when the estimator's tools (gem5, the RISC-V toolchain, llvm-mca)
are absent, the tool reports this in its output, which the caller surfaces.
"""

from __future__ import annotations

import os
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

from .project import Project, is_frozen


@dataclass
class CycleResult:
    status: str  # "ran" | "skipped" | "failed"
    log: str


def run_cycle_estimation(
    project: Project,
    setup: str,
    timeout_s: int = 900,
    setups_dir: Optional[Path] = None,
    build_dir: Optional[Path] = None,
) -> CycleResult:
    if is_frozen():
        return CycleResult("skipped", "Cycle estimation is unavailable in the bundled application.")

    # The estimator reports its own missing tools (gem5, RISC-V toolchain, and
    # llvm-mca only when a workload needs it) in its output, so it is always run
    # here and its log is surfaced to the caller.
    tool_dir = project.root / "tools" / "cycle_estimation"
    main_py = tool_dir / "main.py"
    if not main_py.is_file():
        return CycleResult("skipped", f"Cycle estimation tool not found: {main_py}")

    env = dict(os.environ)
    env["CE_SETUPS_DIR"] = str(setups_dir or project.setups_dir)
    env["CE_CONFIGS_DIR"] = str(project.configs_dir)
    env["CE_BUILD_DIR"] = str(build_dir or (project.build_dir / "cycle_estimation"))
    env["CE_ONLY_SETUP"] = setup

    try:
        proc = subprocess.run(
            [sys.executable, str(main_py)], cwd=str(tool_dir), env=env,
            capture_output=True, text=True, timeout=timeout_s,
        )
    except (subprocess.TimeoutExpired, OSError) as exc:
        return CycleResult("failed", str(exc))

    log = (proc.stdout or "") + (proc.stderr or "")
    if proc.returncode != 0:
        return CycleResult("failed", log)
    # The estimator exits 0 when it skips for a missing tool and reports this in
    # its output; detect that marker to distinguish a skip from a real run.
    if "Skipping cycle estimation" in log:
        return CycleResult("skipped", log)
    return CycleResult("ran", log)
