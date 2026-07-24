"""Compile a workspace setup's libsetup.so via CMake (BUILD_SETUP + SETUPS_DIR).

Shares the repository's build/ tree; the Makefile resets BUILD_SETUP and
SETUPS_DIR so a later `make` still builds the full project. Requires
SYSTEMC_PATH in the environment, as the rest of the build does.
"""

from __future__ import annotations

import subprocess
from dataclasses import dataclass

from .project import Project


@dataclass
class BuildResult:
    ok: bool
    log: str


def build_setup(project: Project, name: str, timeout_s: int = 900) -> BuildResult:
    build_dir = project.root / "build"
    build_dir.mkdir(parents=True, exist_ok=True)
    env = project.child_env()

    configure = [
        "cmake",
        "-S", str(project.root),
        "-B", str(build_dir),
        "-DCMAKE_BUILD_TYPE=Release",
        f"-DBUILD_SETUP={name}",
        f"-DSETUPS_DIR={project.setups_dir}",
    ]
    build = ["cmake", "--build", str(build_dir)]

    log_parts = []
    for command in (configure, build):
        try:
            proc = subprocess.run(
                command, cwd=project.root, env=env, capture_output=True,
                text=True, timeout=timeout_s,
            )
        except (subprocess.TimeoutExpired, OSError) as exc:
            return BuildResult(False, "\n".join(log_parts) + f"\n{exc}")
        log_parts.append(f"$ {' '.join(command)}\n{proc.stdout}\n{proc.stderr}")
        if proc.returncode != 0:
            return BuildResult(False, "\n".join(log_parts))

    lib = project.setups_dir / name / "libsetup.so"
    ok = lib.is_file()
    if not ok:
        log_parts.append(f"\nExpected library not found: {lib}")
    return BuildResult(ok, "\n".join(log_parts))
