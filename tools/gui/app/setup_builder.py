"""Compile a workspace setup's libsetup.so via CMake (BUILD_SETUP + SETUPS_DIR).

Shares the repository's build/ tree; the Makefile resets BUILD_SETUP and
SETUPS_DIR so a later `make` still builds the full project. Requires
SYSTEMC_PATH in the environment, as the rest of the build does.
"""

from __future__ import annotations

import hashlib
import subprocess
from dataclasses import dataclass

from .project import Project


@dataclass
class BuildResult:
    ok: bool
    log: str
    note: str = ""


def _source_hash(project: Project, name: str) -> str:
    """Hash a setup's program sources to decide whether libsetup.so is stale.

    Covers src/ and include/ (program.cpp and any headers); system.yaml is not
    part of the plugin, as overrides are applied by the simulator at run time.
    """
    setup_dir = project.setups_dir / name
    digest = hashlib.sha1()
    for sub in ("src", "include"):
        base = setup_dir / sub
        if not base.is_dir():
            continue
        for path in sorted(p for p in base.rglob("*") if p.is_file()):
            digest.update(str(path.relative_to(setup_dir)).encode())
            digest.update(path.read_bytes())
    return digest.hexdigest()


def build_setup_if_needed(project: Project, name: str, timeout_s: int = 900) -> BuildResult:
    """Build the setup plugin only when its sources changed since the last build.

    The compiled libsetup.so depends only on the program sources, so a run
    reuses it across parameter overrides. Returns an up-to-date result without
    invoking CMake when the stored source hash matches and the library exists.
    """
    lib = project.setups_dir / name / "libsetup.so"
    hash_file = project.build_dir / f"{name}.buildhash"
    current = _source_hash(project, name)
    if lib.is_file() and hash_file.is_file() and hash_file.read_text().strip() == current:
        return BuildResult(True, f"Setup '{name}' is up-to-date. Skipping compilation.")

    result = build_setup(project, name, timeout_s)
    if result.ok:
        hash_file.parent.mkdir(parents=True, exist_ok=True)
        hash_file.write_text(current)
    return result


def build_setup(project: Project, name: str, timeout_s: int = 900) -> BuildResult:
    build_dir = project.build_dir
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
    if project.yaml_cpp_dir is not None:
        configure.append(f"-DYAML_CPP_DIR={project.yaml_cpp_dir}")
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
