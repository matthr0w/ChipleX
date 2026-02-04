import hashlib
import re
from logging import log_error, log_info, log_warn
from pathlib import Path
from typing import Any, Dict, List

import yaml
from constants import *
from classes import *


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
    # TODO: Use overridden params from system.yaml 
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

    results = []
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