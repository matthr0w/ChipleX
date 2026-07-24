"""In-memory mutation helpers for a system.yaml document.

Keep the document consistent, e.g. removing a chiplet drops connections that
reference it.
"""

from __future__ import annotations

from typing import Any, Dict, List, Optional, Tuple


def chiplets(doc: Dict[str, Any]) -> List[Dict[str, Any]]:
    return doc.setdefault("chiplets", [])


def connections(doc: Dict[str, Any]) -> List[Dict[str, Any]]:
    return doc.setdefault("connections", [])


def chiplet_by_name(doc: Dict[str, Any], name: str) -> Optional[Dict[str, Any]]:
    for chiplet in chiplets(doc):
        if chiplet.get("name") == name:
            return chiplet
    return None


def _unique_name(existing: List[str], base: str) -> str:
    if base not in existing:
        return base
    n = 0
    while f"{base}{n}" in existing:
        n += 1
    return f"{base}{n}"


def add_chiplet(doc: Dict[str, Any], type_name: str) -> str:
    names = [c.get("name") for c in chiplets(doc)]
    name = _unique_name(names, "chiplet")
    chiplets(doc).append({"name": name, "type": type_name, "interconnects": []})
    return name


def remove_chiplet(doc: Dict[str, Any], name: str) -> None:
    doc["chiplets"] = [c for c in chiplets(doc) if c.get("name") != name]
    doc["connections"] = [
        conn for conn in connections(doc)
        if not any(ep.split(".")[0] == name for ep in conn.get("endpoints", []))
    ]


def rename_chiplet(doc: Dict[str, Any], old: str, new: str) -> None:
    chiplet = chiplet_by_name(doc, old)
    if chiplet is None or old == new:
        return
    chiplet["name"] = new
    for conn in connections(doc):
        conn["endpoints"] = [
            f"{new}.{ep.split('.', 1)[1]}" if ep.split(".")[0] == old else ep
            for ep in conn.get("endpoints", [])
        ]


def add_interconnect(doc: Dict[str, Any], chiplet_name: str, ic_type: str) -> Optional[str]:
    chiplet = chiplet_by_name(doc, chiplet_name)
    if chiplet is None:
        return None
    ics = chiplet.setdefault("interconnects", [])
    name = _unique_name([i.get("name") for i in ics], "link")
    ics.append({"name": name, "type": ic_type})
    return name


def remove_interconnect(doc: Dict[str, Any], chiplet_name: str, ic_name: str) -> None:
    chiplet = chiplet_by_name(doc, chiplet_name)
    if chiplet is None:
        return
    chiplet["interconnects"] = [i for i in chiplet.get("interconnects", []) if i.get("name") != ic_name]
    endpoint = f"{chiplet_name}.{ic_name}"
    doc["connections"] = [
        conn for conn in connections(doc) if endpoint not in conn.get("endpoints", [])
    ]


def rename_interconnect(doc: Dict[str, Any], chiplet_name: str, old: str, new: str) -> None:
    chiplet = chiplet_by_name(doc, chiplet_name)
    if chiplet is None or old == new:
        return
    for ic in chiplet.get("interconnects", []) or []:
        if ic.get("name") == old:
            ic["name"] = new
    old_ep, new_ep = f"{chiplet_name}.{old}", f"{chiplet_name}.{new}"
    for conn in connections(doc):
        conn["endpoints"] = [new_ep if ep == old_ep else ep for ep in conn.get("endpoints", [])]


def rename_accelerator(doc: Dict[str, Any], chiplet_name: str, old: str, new: str) -> None:
    chiplet = chiplet_by_name(doc, chiplet_name)
    if chiplet is None or old == new:
        return
    for accel in chiplet.get("accelerators", []) or []:
        if accel.get("name") == old:
            accel["name"] = new


def add_accelerator(doc: Dict[str, Any], chiplet_name: str, accel_type: str) -> Optional[str]:
    chiplet = chiplet_by_name(doc, chiplet_name)
    if chiplet is None:
        return None
    accels = chiplet.setdefault("accelerators", [])
    name = _unique_name([a.get("name") for a in accels], accel_type)
    accels.append({"name": name, "type": accel_type})
    return name


def remove_accelerator(doc: Dict[str, Any], chiplet_name: str, accel_name: str) -> None:
    chiplet = chiplet_by_name(doc, chiplet_name)
    if chiplet is None:
        return
    chiplet["accelerators"] = [a for a in chiplet.get("accelerators", []) if a.get("name") != accel_name]
    if not chiplet["accelerators"]:
        chiplet.pop("accelerators", None)


def endpoints(doc: Dict[str, Any]) -> List[str]:
    result: List[str] = []
    for chiplet in chiplets(doc):
        for ic in chiplet.get("interconnects", []) or []:
            result.append(f"{chiplet.get('name')}.{ic.get('name')}")
    return result


def interconnect_type_of(doc: Dict[str, Any], endpoint: str) -> Optional[str]:
    chiplet_name, _, ic_name = endpoint.partition(".")
    chiplet = chiplet_by_name(doc, chiplet_name)
    if chiplet is None:
        return None
    for ic in chiplet.get("interconnects", []) or []:
        if ic.get("name") == ic_name:
            return ic.get("type")
    return None


def add_connection(doc: Dict[str, Any], ep0: str, ep1: str) -> Tuple[bool, str]:
    if ep0 == ep1:
        return False, "A connection needs two different endpoints."
    if interconnect_type_of(doc, ep0) != interconnect_type_of(doc, ep1):
        return False, "Endpoints must share the same interconnect type."
    pair = {ep0, ep1}
    for conn in connections(doc):
        if set(conn.get("endpoints", [])) == pair:
            return False, "That connection already exists."
    connections(doc).append({"endpoints": [ep0, ep1]})
    return True, ""


def remove_connection(doc: Dict[str, Any], index: int) -> None:
    conns = connections(doc)
    if 0 <= index < len(conns):
        del conns[index]
