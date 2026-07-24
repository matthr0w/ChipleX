"""Addressable, per-instance view of a setup's configuration.

Each instance (chiplet, accelerator, interconnect, connection) merges its type
default under configs/ with an optional config: block in system.yaml; only keys
present in the type file are meaningful. Overrides are applied by patching a copy
of system.yaml, the same path the simulator merges.
"""

from __future__ import annotations

import copy
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, List, Optional

import yaml

from .project import Project

_KEY_LINE = re.compile(r"^(\s*)([A-Za-z0-9_-]+):\s*(.*)$")

DEFAULT_WIRE_LENGTH_MM = 1.0
DEFAULT_BER_SCALAR = 1.0


@dataclass(frozen=True)
class ParamRef:
    id: str
    label: str
    scope: str  # "chiplet" | "accelerator" | "interconnect" | "connection"
    value_type: str  # "int" | "float" | "bool"
    default: Any
    chiplet_index: Optional[int] = None
    sub_kind: Optional[str] = None  # "accelerators" | "interconnects"
    sub_index: Optional[int] = None
    conn_index: Optional[int] = None
    dotted_key: Optional[str] = None
    special: Optional[str] = None  # "ber_scalar" | "wire_length_mm"
    unit: Optional[str] = None

    def parse(self, text: str) -> Any:
        text = text.strip()
        if self.value_type == "bool":
            return text.lower() in ("1", "true", "yes", "on")
        if self.value_type == "int":
            return int(float(text))
        return float(text)

    def axis_name(self) -> str:
        """Compact, unique parameter name for run labels.

        Chiplet, accelerator, and interconnect labels are already the module
        path (e.g. "chiplet1.pulp.num_channels"). Connection labels are reduced
        to the endpoint pair plus the key so distinct connections stay
        distinguishable (e.g. "fpga.pulp<->chiplet1.pulp.clk_cycle").
        """
        if self.scope == "cli":
            return self.special
        if self.scope == "connection":
            tail = self.label.split("] ", 1)[-1]
            return tail.replace(" <-> ", "<->").replace(" ", "")
        return self.label


@dataclass
class Instance:
    name: str
    type_name: str
    scope: str
    params: List[ParamRef] = field(default_factory=list)
    children: List["Instance"] = field(default_factory=list)


class SystemModel:
    def __init__(self, project: Project, setup: str):
        self.project = project
        self.setup = setup
        self.system_path = project.setup_dir(setup) / "system.yaml"
        self.doc: Dict[str, Any] = yaml.safe_load(self.system_path.read_text()) or {}
        self.instances: List[Instance] = []
        self._type_cache: Dict[Path, Dict[str, Any]] = {}
        self._unit_cache: Dict[Path, Dict[str, str]] = {}
        self._build()

    # -- construction -----------------------------------------------------

    def _type_path(self, kind: str, type_name: str) -> Path:
        return self.project.configs_dir / kind / f"{type_name}.yaml"

    def _type_file(self, kind: str, type_name: str) -> Dict[str, Any]:
        path = self._type_path(kind, type_name)
        if path not in self._type_cache:
            self._type_cache[path] = yaml.safe_load(path.read_text()) or {} if path.is_file() else {}
        return self._type_cache[path]

    def _units(self, kind: str, type_name: str) -> Dict[str, str]:
        path = self._type_path(kind, type_name)
        if path not in self._unit_cache:
            self._unit_cache[path] = _parse_units(path.read_text()) if path.is_file() else {}
        return self._unit_cache[path]

    def _build(self) -> None:
        for ci, chiplet in enumerate(self.doc.get("chiplets", [])):
            name = chiplet.get("name", f"chiplet{ci}")
            type_name = chiplet.get("type", "")
            type_file = self._type_file("chiplets", type_name)
            effective = _deep_merge(type_file, chiplet.get("config", {}))

            node = Instance(name=name, type_name=type_name, scope="chiplet")
            node.params = self._params_for(
                type_file, self._units("chiplets", type_name), effective, scope="chiplet",
                label_prefix=name, id_prefix=f"chiplet:{ci}", chiplet_index=ci,
            )

            for si, accel in enumerate(chiplet.get("accelerators", []) or []):
                a_name = accel.get("name", f"accel{si}")
                a_type = accel.get("type", "")
                a_file = self._type_file("accelerators", a_type)
                a_eff = _deep_merge(a_file, accel.get("config", {}))
                child = Instance(name=a_name, type_name=a_type, scope="accelerator")
                child.params = self._params_for(
                    a_file, self._units("accelerators", a_type), a_eff, scope="accelerator",
                    label_prefix=f"{name}.{a_name}", id_prefix=f"accel:{ci}:{si}",
                    chiplet_index=ci, sub_kind="accelerators", sub_index=si,
                )
                node.children.append(child)

            for si, ic in enumerate(chiplet.get("interconnects", []) or []):
                i_name = ic.get("name", f"ic{si}")
                i_type = ic.get("type", "")
                i_file = self._type_file("interconnects", i_type)
                i_eff = _deep_merge(i_file, ic.get("config", {}))
                child = Instance(name=i_name, type_name=i_type, scope="interconnect")
                child.params = self._params_for(
                    i_file, self._units("interconnects", i_type), i_eff, scope="interconnect",
                    label_prefix=f"{name}.{i_name}", id_prefix=f"ic:{ci}:{si}",
                    chiplet_index=ci, sub_kind="interconnects", sub_index=si,
                )
                node.children.append(child)

            self.instances.append(node)

        self._build_connections()

    def _build_connections(self) -> None:
        for idx, conn in enumerate(self.doc.get("connections", [])):
            endpoints = conn.get("endpoints", [])
            if len(endpoints) != 2:
                continue
            ep0 = str(endpoints[0])
            ep1 = str(endpoints[1])
            i_type = self._interconnect_type_of_endpoint(ep0)
            i_file = self._type_file("interconnects", i_type) if i_type else {}
            base = self._endpoint_effective_config(ep0)
            effective = _deep_merge(base, conn.get("config", {}))

            label = f"connection[{idx}] {ep0} <-> {ep1}"
            i_units = self._units("interconnects", i_type) if i_type else {}
            node = Instance(name=label, type_name=i_type or "", scope="connection")
            node.params = self._params_for(
                i_file, i_units, effective, scope="connection", label_prefix=label,
                id_prefix=f"conn:{idx}", conn_index=idx,
            )
            node.params.append(
                ParamRef(
                    id=f"conn:{idx}:ber_scalar", label=f"{label}.ber_scalar",
                    scope="connection", value_type="float",
                    default=float(conn.get("ber_scalar", DEFAULT_BER_SCALAR)),
                    conn_index=idx, special="ber_scalar",
                )
            )
            node.params.append(
                ParamRef(
                    id=f"conn:{idx}:wire_length_mm", label=f"{label}.wire_length_mm",
                    scope="connection", value_type="float",
                    default=float(conn.get("wire_length_mm", DEFAULT_WIRE_LENGTH_MM)),
                    conn_index=idx, special="wire_length_mm", unit="mm",
                )
            )
            self.instances.append(node)

    def _params_for(self, type_file, units, effective, scope, label_prefix, id_prefix, **locators) -> List[ParamRef]:
        params: List[ParamRef] = []
        for dotted, value_type, _default in _flatten(type_file):
            current = _get_dotted(effective, dotted, _default)
            params.append(
                ParamRef(
                    id=f"{id_prefix}:{dotted}",
                    label=f"{label_prefix}.{dotted}",
                    scope=scope,
                    value_type=value_type,
                    default=current,
                    dotted_key=dotted,
                    unit=units.get(dotted),
                    **locators,
                )
            )
        return params

    def _interconnect_type_of_endpoint(self, endpoint: str) -> Optional[str]:
        chiplet_name, _, ic_name = endpoint.partition(".")
        for chiplet in self.doc.get("chiplets", []):
            if chiplet.get("name") == chiplet_name:
                for ic in chiplet.get("interconnects", []) or []:
                    if ic.get("name") == ic_name:
                        return ic.get("type")
        return None

    def _endpoint_effective_config(self, endpoint: str) -> Dict[str, Any]:
        chiplet_name, _, ic_name = endpoint.partition(".")
        for chiplet in self.doc.get("chiplets", []):
            if chiplet.get("name") == chiplet_name:
                for ic in chiplet.get("interconnects", []) or []:
                    if ic.get("name") == ic_name:
                        base = self._type_file("interconnects", ic.get("type", ""))
                        return _deep_merge(base, ic.get("config", {}))
        return {}

    # -- queries ----------------------------------------------------------

    def parameters(self) -> List[ParamRef]:
        result: List[ParamRef] = []
        for inst in self.instances:
            result.extend(inst.params)
            for child in inst.children:
                result.extend(child.params)
        return result


def apply_override(doc: Dict[str, Any], ref: ParamRef, value: Any) -> None:
    """Patch a system.yaml document in place for one override."""
    if ref.scope == "connection":
        conn = doc["connections"][ref.conn_index]
        if ref.special:
            conn[ref.special] = value
        else:
            _set_dotted(conn.setdefault("config", {}), ref.dotted_key, value)
        return

    chiplet = doc["chiplets"][ref.chiplet_index]
    if ref.sub_kind:
        target = chiplet[ref.sub_kind][ref.sub_index]
    else:
        target = chiplet
    _set_dotted(target.setdefault("config", {}), ref.dotted_key, value)


def render(doc: Dict[str, Any]) -> str:
    from .setup_writer import dump_system

    return dump_system(doc)


# -- yaml helpers ---------------------------------------------------------


def _parse_units(text: str) -> Dict[str, str]:
    """Extract inline-comment units keyed by dotted path from a type YAML file.

    Type files annotate scalars with a trailing comment, e.g. `size: 256 # bytes`.
    Indentation is tracked so nested keys resolve to their full dotted path.
    Standalone comment lines are ignored; only leaves with an inline comment
    contribute a unit.
    """
    units: Dict[str, str] = {}
    stack: List[tuple] = []  # (indent, key) for the enclosing maps
    for line in text.splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        match = _KEY_LINE.match(line)
        if not match:
            continue
        indent = len(match.group(1))
        key = match.group(2)
        rest = match.group(3)
        while stack and stack[-1][0] >= indent:
            stack.pop()
        value_part, _, comment = rest.partition("#")
        if value_part.strip() == "":
            stack.append((indent, key))
            continue
        unit = comment.strip()
        if unit:
            dotted = ".".join([k for _, k in stack] + [key])
            units[dotted] = unit
    return units


def _deep_merge(base: Dict[str, Any], override: Dict[str, Any]) -> Dict[str, Any]:
    result = copy.deepcopy(base) if isinstance(base, dict) else {}
    for key, value in (override or {}).items():
        if isinstance(value, dict) and isinstance(result.get(key), dict):
            result[key] = _deep_merge(result[key], value)
        else:
            result[key] = copy.deepcopy(value)
    return result


def _flatten(node: Any, prefix: str = "") -> List[tuple]:
    out: List[tuple] = []
    if isinstance(node, dict):
        for key, child in node.items():
            child_prefix = f"{prefix}.{key}" if prefix else str(key)
            out.extend(_flatten(child, child_prefix))
    elif isinstance(node, bool):
        out.append((prefix, "bool", node))
    elif isinstance(node, int):
        out.append((prefix, "int", node))
    elif isinstance(node, float):
        out.append((prefix, "float", node))
    return out


def _get_dotted(data: Dict[str, Any], dotted_key: str, fallback: Any) -> Any:
    node = data
    for key in dotted_key.split("."):
        if not isinstance(node, dict) or key not in node:
            return fallback
        node = node[key]
    return node


def _set_dotted(data: Dict[str, Any], dotted_key: str, value: Any) -> None:
    keys = dotted_key.split(".")
    node = data
    for key in keys[:-1]:
        child = node.get(key)
        if not isinstance(child, dict):
            child = {}
            node[key] = child
        node = child
    node[keys[-1]] = value
