"""Parse the simulator's statistics JSON into a flat map of dotted metric
names to numeric values, for tabulating and plotting across runs."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Dict, List


def load(path: Path) -> Dict[str, Any]:
    return json.loads(Path(path).read_text())


def flatten(data: Any, prefix: str = "") -> Dict[str, float]:
    """Collapse the nested stats document to dotted-name numeric leaves."""
    out: Dict[str, float] = {}
    if isinstance(data, dict):
        for key, child in data.items():
            child_prefix = f"{prefix}.{key}" if prefix else str(key)
            out.update(flatten(child, child_prefix))
    elif isinstance(data, bool):
        out[prefix] = 1.0 if data else 0.0
    elif isinstance(data, (int, float)):
        out[prefix] = float(data)
    return out


def flatten_file(path: Path) -> Dict[str, float]:
    return flatten(load(path))


def union_metric_names(metric_maps: List[Dict[str, float]]) -> List[str]:
    names: set = set()
    for m in metric_maps:
        names |= set(m)
    return sorted(names)
