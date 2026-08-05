"""Discover the gem5 CPU-model manifests used by the cycle estimation.

Models live alongside the cycle-estimation tool (tools/cycle_estimation/gem5/
models/*.yaml). Each manifest names the model and declares the gem5 parameters
it exposes with defaults; the setup editor offers these as the per-chiplet gem5
block. Parameters are model-specific and are not part of the chiplet default
config.
"""

from __future__ import annotations

from typing import Any, Dict, List

import yaml

from .project import Project

# Mirrors DEFAULT_MODEL in tools/cycle_estimation/constants.py.
DEFAULT_MODEL = "riscv-minor"


def _models_dir(project: Project):
    return project.root / "tools" / "cycle_estimation" / "gem5" / "models"


def list_models(project: Project) -> List[str]:
    """Return the available model names, always including the default."""
    directory = _models_dir(project)
    names: List[str] = []
    if directory.is_dir():
        for path in sorted(directory.glob("*.yaml")):
            data = yaml.safe_load(path.read_text()) or {}
            names.append(data.get("name", path.stem.replace("_", "-")))
    if DEFAULT_MODEL not in names:
        names.insert(0, DEFAULT_MODEL)
    return names


def model_params(project: Project, model_name: str) -> Dict[str, Any]:
    """Return the gem5 parameter defaults declared by a model manifest."""
    directory = _models_dir(project)
    if not directory.is_dir():
        return {}
    for path in directory.glob("*.yaml"):
        data = yaml.safe_load(path.read_text()) or {}
        if data.get("name") == model_name:
            return dict(data.get("params") or {})
    fallback = directory / f"{model_name.replace('-', '_')}.yaml"
    if fallback.is_file():
        return dict((yaml.safe_load(fallback.read_text()) or {}).get("params") or {})
    return {}
