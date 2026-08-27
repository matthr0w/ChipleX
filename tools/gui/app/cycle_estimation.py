"""Run the cycle-estimation tool for a setup, mirroring `make run`.

Estimation runs before every simulation so a run's workload cycle counts stay
consistent with the exact system.yaml the simulator loads. Because each run
estimates against its own sandbox (with that run's overrides applied), the
caller can point it at the sandbox's setups directory. The estimator runs from
source as main.py and in a bundle as a standalone executable; when its tools
(gem5, the workload compiler, llvm-mca) are absent, it reports this in its
output, which the caller surfaces.
"""

from __future__ import annotations

import subprocess
import sys
from dataclasses import dataclass
from enum import Enum
from pathlib import Path
from typing import Optional

from .project import Project, base_child_env, is_frozen


class EstimationStatus(Enum):
    """Outcome of running the estimator, keyed by its process exit code.

    SKIPPED mirrors EXIT_SKIPPED in tools/cycle_estimation/constants.py.
    """

    SUCCESS = 0
    FAILED = 1
    SKIPPED = 2

    @classmethod
    def from_exit_code(cls, code: int) -> "EstimationStatus":
        """Map a process exit code to a status; unknown codes are failures."""
        try:
            return cls(code)
        except ValueError:
            return cls.FAILED


@dataclass
class CycleResult:
    status: EstimationStatus
    log: str


def run_cycle_estimation(
    project: Project,
    setup: str,
    timeout_s: int = 900,
    setups_dir: Optional[Path] = None,
    build_dir: Optional[Path] = None,
) -> CycleResult:
    # The estimator reports its own missing tools (gem5, RISC-V toolchain, and
    # llvm-mca only when a workload needs it) in its output, so it is always run
    # here and its log is surfaced to the caller. In a bundle it is a standalone
    # PyInstaller executable (no system Python); from source it is main.py run
    # with the current interpreter.
    if is_frozen():
        exe = project.root / "tools" / "cycle-estimation"
        if not exe.is_file():
            return CycleResult(
                EstimationStatus.SKIPPED, f"Cycle estimation tool not found: {exe}"
            )
        command = [str(exe)]
        cwd = str(exe.parent)
    else:
        tool_dir = project.root / "tools" / "cycle_estimation"
        main_py = tool_dir / "main.py"
        if not main_py.is_file():
            return CycleResult(
                EstimationStatus.SKIPPED, f"Cycle estimation tool not found: {main_py}"
            )
        command = [sys.executable, str(main_py)]
        cwd = str(tool_dir)

    env = base_child_env()
    env["CE_SETUPS_DIR"] = str(setups_dir or project.setups_dir)
    env["CE_CONFIGS_DIR"] = str(project.configs_dir)
    env["CE_BUILD_DIR"] = str(build_dir or (project.build_dir / "cycle_estimation"))
    env["CE_ONLY_SETUP"] = setup
    if project.user_models_dir is not None:
        env["CE_USER_MODELS_DIR"] = str(project.user_models_dir)

    try:
        proc = subprocess.run(
            command,
            cwd=cwd,
            env=env,
            capture_output=True,
            text=True,
            timeout=timeout_s,
        )
    except (subprocess.TimeoutExpired, OSError) as exc:
        return CycleResult(EstimationStatus.FAILED, str(exc))

    log = (proc.stdout or "") + (proc.stderr or "")
    return CycleResult(EstimationStatus.from_exit_code(proc.returncode), log)
