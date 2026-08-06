"""Discover the gem5 CPU-model manifests used by the cycle estimation.

Models live alongside the cycle-estimation tool (tools/cycle_estimation/gem5/
models/*.yaml). Each manifest names the model and declares the gem5 parameters
it exposes with defaults; the setup editor offers these as the per-chiplet gem5
block. Parameters are model-specific and are not part of the chiplet default
config.

Bundle installs also read a persistent, user-writable directory
(project.user_models_dir) so users can add or override CPU models; it is
searched before the bundled models. This mirrors the estimator's own search
order in tools/cycle_estimation/utils.py.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any, Dict, List

import yaml

from .project import Project

# Mirrors DEFAULT_MODEL in tools/cycle_estimation/constants.py.
DEFAULT_MODEL = "riscv-minor"


def _models_dirs(project: Project) -> List[Path]:
    """Model directories, most specific first (user overrides, then bundled)."""
    dirs: List[Path] = []
    if project.user_models_dir is not None:
        dirs.append(project.user_models_dir)
    bundled = project.gem5_models_dir or (
        project.root / "tools" / "cycle_estimation" / "gem5" / "models"
    )
    dirs.append(bundled)
    return dirs


def list_models(project: Project) -> List[str]:
    """Return the available model names, always including the default."""
    names: List[str] = []
    seen: set = set()
    for directory in _models_dirs(project):
        if not directory.is_dir():
            continue
        for path in sorted(directory.glob("*.yaml")):
            data = yaml.safe_load(path.read_text()) or {}
            name = data.get("name", path.stem.replace("_", "-"))
            if name not in seen:
                seen.add(name)
                names.append(name)
    if DEFAULT_MODEL not in names:
        names.insert(0, DEFAULT_MODEL)
    return names


def _manifest(project: Project, model_name: str) -> Dict[str, Any]:
    """Return the raw manifest for a model, or {} when none is found."""
    for directory in _models_dirs(project):
        if not directory.is_dir():
            continue
        for path in directory.glob("*.yaml"):
            data = yaml.safe_load(path.read_text()) or {}
            if data.get("name") == model_name:
                return data
        fallback = directory / f"{model_name.replace('-', '_')}.yaml"
        if fallback.is_file():
            return yaml.safe_load(fallback.read_text()) or {}
    return {}


def model_params(project: Project, model_name: str) -> Dict[str, Any]:
    """Return the gem5 parameter defaults declared by a model manifest."""
    return dict(_manifest(project, model_name).get("params") or {})


def model_description(project: Project, model_name: str) -> str:
    """Return the model manifest's description, or '' when none is declared."""
    return str(_manifest(project, model_name).get("description") or "")
