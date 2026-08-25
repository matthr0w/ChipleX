import hashlib
import re
import shutil
from pathlib import Path
from typing import Any, Dict, List

import yaml
from ce_logging import log_error, log_info, log_warn
from classes import *
from constants import *


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

def model_search_dirs() -> List[Path]:
    """Directories searched for CPU-model manifests, most specific first.

    The optional user models directory (bundle installs) is searched before the
    bundled models so users can add or override CPU models.
    """
    dirs: List[Path] = []
    if USER_MODELS_DIR:
        dirs.append(USER_MODELS_DIR)
    dirs.append(GEM5_MODELS_DIR)
    return dirs

def load_model(name: str) -> dict:
    """Load a CPU-model manifest by name (e.g. 'riscv-minor')."""
    filename = f"{name.replace('-', '_')}.yaml"
    for directory in model_search_dirs():
        manifest = directory / filename
        if manifest.exists():
            model = load_yaml(manifest)
            # Record the manifest's directory so model_config can resolve a
            # config script that ships alongside a user-provided model.
            model["_dir"] = str(directory)
            return model
    searched = ", ".join(str(d) for d in model_search_dirs())
    log_error(f"Unknown CPU model '{name}': no manifest '{filename}' in {searched}.")

def model_config(model: dict) -> Path:
    """Resolve the gem5 config script named by the model.

    Relative paths are resolved against the manifest's own directory first (so a
    user model can ship its own script), then its parent, then the bundled gem5
    directory that holds the reference se_model.py.
    """
    config = model.get("config", "se_model.py")
    path = Path(config)
    if path.is_absolute():
        return path
    candidates: List[Path] = []
    src_dir = model.get("_dir")
    if src_dir:
        candidates.append(Path(src_dir) / path)
        candidates.append(Path(src_dir).parent / path)
    candidates.append(GEM5_DIR / path)
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return GEM5_DIR / path

def model_cli(model: dict, params: Dict[str, Any]) -> List[str]:
    """Build the config-script flags from the model's cpu, mem mode, and params.

    `params` are the resolved gem5 parameters (model defaults with the chiplet's
    gem5-block overrides applied). --clock and --mem-latency are appended by the
    caller from the resolved SystemC config.
    """
    cli = ["--cpu-type", str(model["cpu_type"])]
    if model.get("mem_mode"):
        cli += ["--mem-mode", str(model["mem_mode"])]
    for key, value in (params or {}).items():
        cli += [f"--{key}", str(value)]
    return cli

def merge_nodes(target: dict, override: dict, path: str = ""):
    """Deep-merge `override` into `target` in place, mirroring SetupLoader::merge_nodes.

    Only keys already present in `target` are overridden; unknown keys warn and
    are ignored, so the tool honors the same override contract as the SystemC
    loader (system.yaml `config:` blocks over configs/chiplets/<type>.yaml).
    """
    if not isinstance(override, dict):
        return
    for key, value in override.items():
        full_path = f"{path}.{key}" if path else key
        if not isinstance(target, dict) or key not in target:
            log_warn(f"Unknown parameter: {full_path}. Ignoring.")
            continue
        if isinstance(value, dict) and isinstance(target[key], dict):
            merge_nodes(target[key], value, full_path)
        else:
            target[key] = value

def resolve_chiplet(setup: Setup, chiplet_name: str):
    """Return (merged_config, gem5_block) for a chiplet in the setup's system.yaml.

    The merged config is configs/chiplets/<type>.yaml with the system.yaml
    chiplet `config:` overrides deep-merged on top (same as the SystemC loader).
    The `gem5:` block is read verbatim; the SystemC loader ignores it.
    """
    system = load_yaml(setup.system_file)
    entry = next((c for c in system.get("chiplets", []) if c.get("name") == chiplet_name), None)
    if entry is None:
        return {}, {}
    base = load_yaml(CHIPLET_CONFIGS / f"{entry.get('type')}.yaml")
    for section, node in (entry.get("config") or {}).items():
        if section in base:
            merge_nodes(base[section], node, section)
        else:
            log_warn(f"Unknown parameter: {section}. Ignoring.")
    return base, (entry.get("gem5") or {})

def load_executors(workload: Workload):
    """Return the (chiplet, module) executors declared for a workload.

    Each workload declares the modules that run its wait_cycles region in a
    sidecar YAML next to its source (workloads/<region>.yaml) with an
    `executors:` list of "chiplet.module" strings. The declaration is
    authoritative; the estimator does not infer executors from the program
    source. A missing or malformed declaration is a hard error.
    """
    sidecar = workload.source_path.with_suffix(".yaml")
    if not sidecar.is_file():
        log_error(f"{workload.source_path}: missing executor declaration '{sidecar.name}'. "
                  f"Create it next to the workload with an 'executors:' list of "
                  f"'chiplet.module' entries naming the modules that run this region.")
    entries = (load_yaml(sidecar) or {}).get("executors")
    if not isinstance(entries, list) or not entries:
        log_error(f"{sidecar}: 'executors:' must be a non-empty list of 'chiplet.module' entries.")

    executors = []
    seen = set()
    for entry in entries:
        chiplet, sep, module = str(entry).partition(".")
        chiplet, module = chiplet.strip(), module.strip()
        if not sep or not chiplet or not module:
            log_error(f"{sidecar}: invalid executor '{entry}'; expected 'chiplet.module'.")
        pair = (chiplet, module)
        if pair not in seen:
            seen.add(pair)
            executors.append(pair)
    return executors

def execution_hash(source_hash: str, model: dict, execution: Execution, accel_params: dict) -> str:
    """Hash the full set of estimation inputs to drive re-estimation.

    Covers the workload source plus everything that can change the result: CPU
    model, compiler and flags, resolved gem5 params, core clock, memory latency,
    bus width, and the executor's accelerator params (its speedup differs by
    accelerator type/config). The executor identity itself is the DB key, not part of the
    hash, so two identically-configured executors share a result. Any change
    re-triggers estimation.
    """
    hash = hashlib.sha1()
    hash.update(source_hash.encode())
    hash.update(str(execution.model).encode())
    hash.update(str(model.get("compiler", RISCV_COMPILER)).encode())
    hash.update(str(model.get("compiler_flags", RISCV_COMPILER_FLAGS)).encode())
    hash.update(str(sorted(execution.params.items())).encode())
    hash.update(execution.clock.encode())
    hash.update(execution.mem_latency.encode())
    hash.update(str(execution.bus_width).encode())
    hash.update(str(sorted((accel_params or {}).items())).encode())
    return hash.hexdigest()

def resolve_workload(setup: Setup, workload: Workload):
    """Build one Execution per (chiplet, module) that runs this workload region.

    Each execution resolves its CPU model, gem5 params, core-clock period,
    backing-memory latency, and invalidation hash from its owning chiplet.
    """
    for chiplet, module in load_executors(workload):
        config, gem5_block = resolve_chiplet(setup, chiplet)

        model_name = gem5_block.get("cpu_model", DEFAULT_MODEL)
        model = load_model(model_name)

        params = dict(model.get("params") or {})
        for key, value in (gem5_block.get("params") or {}).items():
            if key in params:
                params[key] = value
            else:
                log_warn(f"Unknown gem5 param '{key}' for model '{model_name}'. Ignoring.")

        cores = config.get("cores") or {}
        memory = config.get("memory") or {}
        axi = config.get("axi") or {}
        clk_cycle = cores.get("clk_cycle", DEFAULT_CLK_CYCLE_NS)
        access_latency = memory.get("access_latency", DEFAULT_ACCESS_LATENCY_CYCLES)
        mem_clk_cycle = memory.get("clk_cycle", DEFAULT_MEM_CLK_CYCLE_NS)
        # gem5's crossbar width is in bytes; the SystemC AXI width is in bits.
        axi_width_bits = axi.get("width", DEFAULT_AXI_WIDTH_BITS)
        bus_width = max(1, axi_width_bits // 8)

        execution = Execution(
            chiplet=chiplet,
            module=module,
            model=model_name,
            params=params,
            clock=f"{clk_cycle}ns",
            mem_latency=f"{access_latency * mem_clk_cycle}ns",
            bus_width=bus_width,
        )
        # The accelerator's speedup config is part of what determines this
        # executor's result, so it enters the hash. Resolve it quietly: a core
        # executor legitimately has no accelerator.
        accel_params = get_accel_params(setup, chiplet, module, quiet=True)
        execution.input_hash = execution_hash(workload.source_hash, model, execution, accel_params)
        workload.executions.append(execution)

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


def get_accel_params(setup: Setup, chiplet_name: str, module_name: str, quiet: bool = False):
    """Resolve accelerator resource params for one executor (chiplet.module).

    Looks up the accelerator instance in system.yaml, loads its type's config
    from configs/accelerators/, and applies any per-instance `config:` override
    (the same contract as the SystemC loader). Resolving the exact executor
    matters when one workload runs on several accelerator types whose speedups
    differ. `quiet` suppresses the diagnostic warnings when the result is only
    needed for the invalidation hash (a core executor has no accelerator).
    """
    defaults = {
        "accel_type": None,
        "speedup_factor": None,
        "num_alu": 1,
        "num_branch": 1,
        "num_fpalu": 1,
        "num_fpdivsqrt": 1,
        "num_idiv": 1,
        "num_imul": 1,
        "num_mem": 1,
    }

    if not setup.system_file.exists():
        log_error(f"Required system description file for setup '{setup.id}' not found: {setup.system_file}")
    system_data = load_yaml(setup.system_file)

    # Find the accelerator instance for this exact executor.
    accel_entry = None
    for chiplet in system_data.get("chiplets", []):
        if chiplet.get("name") != chiplet_name:
            continue
        for accel in chiplet.get("accelerators", []) or []:
            if accel.get("name") == module_name:
                accel_entry = accel
                break
        break
    if accel_entry is None:
        if not quiet:
            log_warn(f"No accelerator '{chiplet_name}.{module_name}' found in system description.")
        return dict(defaults)

    accel_type = accel_entry.get("type")
    if not accel_type:
        if not quiet:
            log_warn(f"Accelerator '{chiplet_name}.{module_name}' has no type in system description.")
        return dict(defaults)

    # Load the accelerator config and apply the instance's system.yaml override
    # (flat merge, matching the SystemC loader for a flat accelerator config).
    accel_config = ACCELERATOR_CONFIGS / f"{accel_type}.yaml"
    if not accel_config.exists():
        log_error(f"Required config file for accelerator type '{accel_type}' not found: {accel_config}.")
    accel_data = load_yaml(accel_config)
    merge_nodes(accel_data, accel_entry.get("config") or {})

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
        not quiet
        and speedup_factor is None
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