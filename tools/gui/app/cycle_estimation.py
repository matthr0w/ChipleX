"""Run the cycle-estimation tool for a setup, mirroring `make run`.

The command-line flow runs cycle estimation before every simulation; the GUI
runs it as part of building a setup so a setup's workload cycle counts stay in
sync with its workload sources. Estimation needs the RISC-V toolchain (see
project.missing_cycle_tools()) and a Python interpreter, so it is skipped in a
frozen bundle and when the tools are absent.
"""

from __future__ import annotations

import os
import subprocess
import sys
from dataclasses import dataclass

from .project import Project, is_frozen, missing_cycle_tools


@dataclass
class CycleResult:
    status: str  # "ran" | "skipped" | "failed"
    log: str


def run_cycle_estimation(project: Project, setup: str, timeout_s: int = 900) -> CycleResult:
    if is_frozen():
        return CycleResult("skipped", "Cycle estimation is unavailable in the bundled application.")
    missing = missing_cycle_tools()
    if missing:
        return CycleResult("skipped", "Cycle estimation skipped; not installed: " + ", ".join(missing))

    tool_dir = project.root / "tools" / "cycle_estimation"
    main_py = tool_dir / "main.py"
    if not main_py.is_file():
        return CycleResult("skipped", f"Cycle estimation tool not found: {main_py}")

    env = dict(os.environ)
    env["CE_SETUPS_DIR"] = str(project.setups_dir)
    env["CE_CONFIGS_DIR"] = str(project.configs_dir)
    env["CE_BUILD_DIR"] = str(project.build_dir / "cycle_estimation")
    env["CE_ONLY_SETUP"] = setup

    try:
        proc = subprocess.run(
            [sys.executable, str(main_py)], cwd=str(tool_dir), env=env,
            capture_output=True, text=True, timeout=timeout_s,
        )
    except (subprocess.TimeoutExpired, OSError) as exc:
        return CycleResult("failed", str(exc))

    log = (proc.stdout or "") + (proc.stderr or "")
    return CycleResult("ran" if proc.returncode == 0 else "failed", log)
