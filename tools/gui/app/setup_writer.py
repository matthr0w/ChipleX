"""Create setup directories on disk.

New setups get a documented program.cpp template describing how to add
per-chiplet code, so the GUI never has to parse or rewrite C++.
"""

from __future__ import annotations

import copy
import shutil
from pathlib import Path
from typing import Any, Dict

import yaml


class _FlowSeq(list):
    """A list rendered in YAML flow style, e.g. [a, b]."""


class _SystemDumper(yaml.SafeDumper):
    pass


def _flow_seq_representer(dumper: yaml.Dumper, data: _FlowSeq):
    return dumper.represent_sequence("tag:yaml.org,2002:seq", data, flow_style=True)


_SystemDumper.add_representer(_FlowSeq, _flow_seq_representer)


def dump_system(doc: Dict[str, Any]) -> str:
    """Serialize a system.yaml doc, keeping connection endpoints inline."""
    doc = copy.deepcopy(doc)
    for conn in doc.get("connections") or []:
        endpoints = conn.get("endpoints")
        if isinstance(endpoints, list):
            conn["endpoints"] = _FlowSeq(endpoints)
    return yaml.dump(
        doc, Dumper=_SystemDumper, default_flow_style=False, sort_keys=False
    )


PROGRAM_HEADER = """#pragma once

#include "setup/Types.h"

extern "C" ModuleCodeMap *get_program_code();
"""

PROGRAM_TEMPLATE = """#include "program.h"

#include "modules/Core.h"
#include "modules/HWAccel.h"

ModuleCodeMap *get_program_code() {
\tstatic ModuleCodeMap code = {
\t};
\treturn &code;
}
"""


def new_system_doc() -> Dict[str, Any]:
    return {"chiplets": [], "connections": []}


def write_system(setups_dir: Path, name: str, doc: Dict[str, Any]) -> Path:
    setup_dir = Path(setups_dir) / name
    setup_dir.mkdir(parents=True, exist_ok=True)
    system_path = setup_dir / "system.yaml"
    system_path.write_text(dump_system(doc))
    return system_path


def create_setup(setups_dir: Path, name: str, doc: Dict[str, Any]) -> Path:
    setup_dir = Path(setups_dir) / name
    (setup_dir / "src").mkdir(parents=True, exist_ok=True)
    (setup_dir / "include").mkdir(parents=True, exist_ok=True)
    (setup_dir / "workloads").mkdir(parents=True, exist_ok=True)

    write_system(setups_dir, name, doc)
    (setup_dir / "include" / "program.h").write_text(PROGRAM_HEADER)
    program_file = setup_dir / "src" / "program.cpp"
    if not program_file.exists():
        program_file.write_text(PROGRAM_TEMPLATE)
    return setup_dir


def program_path(setups_dir: Path, name: str) -> Path:
    return Path(setups_dir) / name / "src" / "program.cpp"


def delete_setup(setups_dir: Path, name: str) -> None:
    setup_dir = Path(setups_dir) / name
    if setup_dir.is_dir():
        shutil.rmtree(setup_dir)
