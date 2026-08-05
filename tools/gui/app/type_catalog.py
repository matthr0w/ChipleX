"""Available chiplet/accelerator/interconnect types and their config parameters,
read from configs/ as the palette for the setup editor."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import List

from .system_model import _flatten, _parse_constants, _parse_units

KINDS = ("chiplets", "accelerators", "interconnects")


@dataclass(frozen=True)
class TypeParam:
    dotted_key: str
    value_type: str
    default: object
    unit: str | None = None


def list_types(configs_dir: Path, kind: str) -> List[str]:
    directory = configs_dir / kind
    if not directory.is_dir():
        return []
    return sorted(p.stem for p in directory.glob("*.yaml"))


def type_params(configs_dir: Path, kind: str, type_name: str) -> List[TypeParam]:
    import yaml

    path = configs_dir / kind / f"{type_name}.yaml"
    if not path.is_file():
        return []
    data = yaml.safe_load(path.read_text()) or {}
    text = path.read_text()
    units = _parse_units(text)
    constants = _parse_constants(text)
    return [
        TypeParam(dotted, value_type, default, units.get(dotted))
        for dotted, value_type, default in _flatten(data)
        if dotted not in constants
    ]
