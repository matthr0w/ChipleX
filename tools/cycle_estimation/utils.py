import hashlib
import re
import shutil
from logging import log_error, log_info, log_warn
from pathlib import Path
from typing import Any, Dict, List

import yaml
from classes import *
from constants import *


def resolve_gem5_home() -> Path:
    """Locate the gem5 source tree (for the m5op header + per-ABI shim).

    Uses GEM5_HOME when set, otherwise walks up from the gem5 binary until a
    directory containing include/gem5/m5ops.h is found.
    """
    if GEM5_HOME:
        return Path(GEM5_HOME)
    binpath = shutil.which(GEM5_BINARY) or GEM5_BINARY
    for parent in Path(binpath).resolve().parents:
        if (parent / "include" / "gem5" / "m5ops.h").is_file():
            return parent
    return Path("")

def load_model(name: str) -> dict:
    """Load a CPU-model manifest by name (e.g. 'riscv-minor')."""
    manifest = GEM5_MODELS_DIR / f"{name.replace('-', '_')}.yaml"
    if not manifest.exists():
        log_error(f"Unknown CPU model '{name}': manifest not found at {manifest}.")
    return load_yaml(manifest)

def model_config(model: dict) -> Path:
    """Resolve the gem5 config script named by the model."""
    config = model.get("config", "se_model.py")
    path = Path(config)
    return path if path.is_absolute() else GEM5_DIR / path

def model_cli(model: dict) -> List[str]:
    """Build the config-script flags from the model's cpu, mem mode, and params.

    --clock and --mem-latency are appended by the caller from the SystemC config.
    """
    cli = ["--cpu-type", str(model["cpu_type"])]
    if model.get("mem_mode"):
        cli += ["--mem-mode", str(model["mem_mode"])]
    for key, value in (model.get("params") or {}).items():
        cli += [f"--{key}", str(value)]
    return cli

def parse_gem5_cycles(stats_path: Path, num_sections: int) -> Dict[str, int]:
    """Map the first `num_sections` gem5 stat dumps to SECTION1..N cycle counts.

    Each m5_dump_stats call appends a 'Begin Simulation Statistics' block; the
    trailing block from gem5's end-of-run dump is ignored by taking only the
    first num_sections blocks in order.
    """
    if not stats_path.exists():
        return {}
    text = stats_path.read_text(encoding="utf-8")
    cycles: List[int] = []
    for chunk in text.split("Begin Simulation Statistics")[1:]:
        match = re.search(r"system\.cpu\.numCycles\s+(\d+)", chunk)
        if match:
            cycles.append(int(match.group(1)))
    return {f"SECTION{i + 1}": cycles[i] for i in range(min(num_sections, len(cycles)))}

def sha1_file(path: Path) -> str:
    hash = hashlib.sha1()
    with path.open("rb") as file:
        while True:
            chunk = file.read(8192)
            if not chunk:
                break
            hash.update(chunk)
    return hash.hexdigest()

def load_yaml(path: Path) -> dict:
    if not path.exists():
        return {}
    with path.open("r", encoding="utf-8") as f:
        return yaml.safe_load(f) or {}

def write_yaml(path: Path, data: dict):
    with path.open("w", encoding="utf-8") as f:
        yaml.safe_dump(data, f, sort_keys=False)

def get_accel_params(setup: Setup, section_id: str):
    # TODO: Use overwritten params from system.yaml 
    code = setup.program_file.read_text(encoding="utf-8")
    module_defs = set()

    # Extract module definitions (chiplet, module)
    module_pattern = re.compile(r'\{\s*\{\s*"([^"]+)"\s*,\s*"([^"]+)"\s*\}\s*,\s*\{AccelCode\{.*?\}\}\s*\}', re.DOTALL)
    for module_match in module_pattern.finditer(code):
        chiplet_name = module_match.group(1)
        module_name = module_match.group(2)
        module_block = module_match.group(0)

        # Matching only wait_cycles("<section_id>")
        wait_regex = re.compile(r'\.wait_cycles\("' + re.escape(section_id) + r'"\)')
        if wait_regex.search(module_block):
            module_defs.add((chiplet_name, module_name))

    if not module_defs:
        log_warn(f"No accelerator found containing wait_cycles('{section_id}') in program.cpp")
        return {
            "accel_type": None,
            "speedup_factor": None,
            "num_alu": 1,
            "num_branch": 1,
            "num_fpalu": 1,
            "num_fpdivsqrt": 1,
            "num_idiv": 1,
            "num_imul": 1,
            "num_mem": 1
        }

    # Load system.yaml
    if not setup.system_file.exists():
        log_error(f"Required system description file for setup '{setup.id}' not found: {setup.system_file}")
    system_data = load_yaml(setup.system_file)

    chiplets_section = system_data.get("chiplets", [])

    found_types = []

    for chiplet_name, module_name in module_defs:
        for chiplet in chiplets_section:
            if chiplet.get("name") != chiplet_name:
                continue

            accels = chiplet.get("accelerators", [])
            for accel in accels:
                if accel.get("name") == module_name:
                    accel_type = accel.get("type")
                    if accel_type:
                        found_types.append(accel_type)

    if not found_types:
        log_warn(f"No corresponding accelerator type found in system description.")
        return {
            "accel_type": None,
            "speedup_factor": None,
            "num_alu": 1,
            "num_branch": 1,
            "num_fpalu": 1,
            "num_fpdivsqrt": 1,
            "num_idiv": 1,
            "num_imul": 1,
            "num_mem": 1
        }

    if len(found_types) > 1:
        log_warn(f"Multiple accelerator types using same workload model. Using first: {found_types[0]}")

    accel_type = found_types[0]

    # Load accelerator config
    accel_config = ACCELERATOR_CONFIGS / f"{accel_type}.yaml"
    if not accel_config.exists():
        log_error(f"Required config file for accelerator type '{accel_type}' not found: {accel_config}.")
    accel_data = load_yaml(accel_config)

    # Extract parameters with defaults
    speedup_factor = accel_data.get("speedup_factor", None)
    num_alu = accel_data.get("num_alu", 1)
    num_branch = accel_data.get("num_branch", 1)
    num_fpalu = accel_data.get("num_fpalu", 1)
    num_fpdivsqrt = accel_data.get("num_fpdivsqrt", 1)
    num_idiv = accel_data.get("num_idiv", 1)
    num_imul = accel_data.get("num_imul", 1)
    num_mem = accel_data.get("num_mem", 1)

    # Warn if accelerator is useless
    if (
        speedup_factor is None
        and num_alu == 1
        and num_branch == 1
        and num_fpalu == 1
        and num_fpdivsqrt == 1
        and num_idiv == 1
        and num_imul == 1
        and num_mem == 1
    ):
        log_warn(f"Accelerator '{accel_type}' has no speedup factor and all unit counts = 1. This likely indicates a poorly configured accelerator.")

    return {
        "accel_type": accel_type,
        "speedup_factor": speedup_factor,
        "num_alu": num_alu,
        "num_branch": num_branch,
        "num_fpalu": num_fpalu,
        "num_fpdivsqrt": num_fpdivsqrt,
        "num_idiv": num_idiv,
        "num_imul": num_imul,
        "num_mem": num_mem
    }