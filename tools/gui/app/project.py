"""Locate the simulator, its setups and configs, and the SystemC runtime env."""

from __future__ import annotations

import os
import shutil
import sys
from dataclasses import dataclass, replace
from pathlib import Path
from typing import Dict, List, Optional


def is_frozen() -> bool:
    """True when running from a PyInstaller bundle."""
    return getattr(sys, "frozen", False) and hasattr(sys, "_MEIPASS")


# Tools required to compile a setup plugin. The cycle-estimation toolchain
# (gem5, RISC-V toolchain, llvm-mca) is validated by the estimator itself, which
# reports what is missing in its output, so it is not probed here.
_CXX_COMPILERS = ("c++", "g++", "clang++")


def missing_build_tools() -> List[str]:
    """Return the build tools that are absent; empty when setups can be built."""
    missing: List[str] = []
    if shutil.which("cmake") is None:
        missing.append("cmake")
    if not any(shutil.which(cxx) for cxx in _CXX_COMPILERS):
        missing.append("C++ compiler (g++ or clang++)")
    return missing


def user_data_dir() -> Path:
    """Persistent, writable location for the workspace and build outputs."""
    base = os.environ.get("XDG_DATA_HOME")
    root = Path(base) if base else Path.home() / ".local" / "share"
    return root / "chiplet-sim"


@dataclass
class Project:
    root: Path
    sim_binary: Path
    configs_dir: Path
    setups_dir: Path
    build_dir: Path
    systemc_path: Optional[Path] = None
    yaml_cpp_dir: Optional[Path] = None

    @classmethod
    def discover(cls, start: Optional[Path] = None) -> "Project":
        """Locate the framework root.

        In a PyInstaller bundle the root is the staged `framework/` directory,
        with a bundled SystemC install and yaml-cpp so setups can be compiled
        offline, and setup builds directed to a persistent user data directory.

        Otherwise the root is the first ancestor of this file that contains both
        a `setups` and a `configs` directory, so the tool works from any working
        directory during development.
        """
        if is_frozen():
            root = Path(sys._MEIPASS) / "framework"
            return cls(
                root=root,
                sim_binary=root / "sim",
                configs_dir=root / "configs",
                setups_dir=root / "setups",
                build_dir=user_data_dir() / "build",
                systemc_path=root / "systemc",
                yaml_cpp_dir=root / "deps" / "yaml-cpp",
            )

        here = (start or Path(__file__)).resolve()
        for candidate in [here, *here.parents]:
            if (candidate / "setups").is_dir() and (candidate / "configs").is_dir():
                root = candidate
                break
        else:
            raise FileNotFoundError(
                "Could not locate the framework root (no ancestor with both "
                "'setups/' and 'configs/')."
            )
        return cls(
            root=root,
            sim_binary=root / "sim",
            configs_dir=root / "configs",
            setups_dir=root / "setups",
            build_dir=root / "build",
        )

    def with_setups_dir(self, setups_dir: Path) -> "Project":
        """Return a copy that reads setups from a different directory.

        The simulator binary and configs stay in the repository; only the setups
        location changes, which is how the GUI operates on a managed workspace.
        """
        return replace(self, setups_dir=Path(setups_dir))

    def seed_workspace(self, workspace_dir: Path) -> "Project":
        """Seed a managed setups workspace from the repository, then use it.

        Copies the tracked setups into `workspace_dir` on first use so the GUI
        can create and edit setups without touching the repository. Returns a
        Project pointing at the workspace.
        """
        workspace_dir = Path(workspace_dir)
        if not workspace_dir.exists():
            shutil.copytree(self.setups_dir, workspace_dir)
        return self.with_setups_dir(workspace_dir)

    def list_setups(self) -> List[str]:
        """Return the names of setups that define a system.yaml."""
        if not self.setups_dir.is_dir():
            return []
        names = [
            p.name
            for p in sorted(self.setups_dir.iterdir())
            if p.is_dir() and (p / "system.yaml").is_file()
        ]
        return names

    def setup_dir(self, setup: str) -> Path:
        return self.setups_dir / setup

    def sim_is_built(self) -> bool:
        return self.sim_binary.is_file() and os.access(self.sim_binary, os.X_OK)

    def setup_is_built(self, setup: str) -> bool:
        return (self.setup_dir(setup) / "libsetup.so").is_file()

    def child_env(self) -> Dict[str, str]:
        """Environment for launching the simulator.

        Inherits the current environment and, when SYSTEMC_PATH is set, ensures
        the SystemC shared library directories are on LD_LIBRARY_PATH so the sim
        can load libsystemc.so. The copyright banner is suppressed to keep
        captured stdout clean.
        """
        env = dict(os.environ)
        env.setdefault("SYSTEMC_DISABLE_COPYRIGHT_MESSAGE", "1")

        if self.systemc_path is not None:
            env["SYSTEMC_PATH"] = str(self.systemc_path)
        systemc_path = env.get("SYSTEMC_PATH", "").strip()
        extra: List[str] = []
        if systemc_path:
            for sub in ("lib", "lib64"):
                lib_dir = Path(systemc_path) / sub
                if lib_dir.is_dir():
                    extra.append(str(lib_dir))
        if extra:
            existing = env.get("LD_LIBRARY_PATH", "")
            parts = extra + ([existing] if existing else [])
            env["LD_LIBRARY_PATH"] = os.pathsep.join(parts)
        return env

    def preflight(self) -> List[str]:
        """Return a list of human-readable problems that would break runs."""
        problems: List[str] = []
        if not self.sim_is_built():
            problems.append(
                f"Simulator not built: {self.sim_binary} is missing or not "
                f"executable. Build it with 'make release'."
            )
        if not self.list_setups():
            problems.append(f"No setups found under {self.setups_dir}.")
        return problems
