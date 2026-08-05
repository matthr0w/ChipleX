#!/usr/bin/env python3
import re
import shutil
import subprocess
import sys
from logging import log_error, log_info, log_warn
from pathlib import Path
from typing import Dict, List

from classes import *
from constants import *
from utils import *


class CycleEstimator:
    def __init__(self):
        self.setups: List[Setup] = self.get_setups()
        self.updates: Dict[Setup, List[Workload]] = self.get_updates()

    def update_database(self, setup: Setup, workload: Workload, execution: Execution):
        data = load_yaml(setup.workloads_db)
        if "workloads" not in data:
            data["workloads"] = {}
        # Entries are nested region -> executor -> {cycles_count, input_hash}.
        region = data["workloads"].setdefault(workload.id, {})
        # Drop any legacy flat keys from the pre-executor schema.
        for legacy in ("cycles_count", "input_hash", "source_hash"):
            region.pop(legacy, None)
        region[execution.executor] = {
            "cycles_count": execution.estimation_result,
            "input_hash": execution.input_hash,
        }
        write_yaml(setup.workloads_db, data)

    def get_setups(self) -> List[Setup]:
        setups: List[Setup] = []
        for setup_dir in sorted(SETUPS_ROOT.iterdir()):
            if not setup_dir.is_dir():
                continue
            # Restrict to a single setup when requested (GUI per-setup builds).
            if ONLY_SETUP and setup_dir.stem != ONLY_SETUP:
                continue
            # Check if workloads exist
            workloads_dir = setup_dir / SETUP_WORKLOADS_SUBDIR
            if not workloads_dir.exists():
                continue

            setup = Setup(
                id=setup_dir.stem,
                path=setup_dir,
                program_file=setup_dir / SETUP_PROGRAM_FILE,
                system_file=setup_dir / SETUP_SYSTEM_FILE,
                workloads_db=setup_dir / SETUP_WORKLOADS_DB
            )

            for cpp_file in sorted(workloads_dir.glob("*.cpp")):
                id = cpp_file.stem
                file_id = f"{setup.id}__{id}"
                workload = Workload(
                    id=id,
                    file_id=file_id,
                    source_path=cpp_file,
                    source_hash=sha1_file(cpp_file),
                    dest_path=BUILD_DIR / f"{file_id}.cpp",
                    asm_path=BUILD_DIR / f"{file_id}.s",
                    binary_path=BUILD_DIR / f"{file_id}",
                )
                # Enumerate the executions (one per chiplet+core that runs this
                # region), each resolving its model, gem5 params, and clock/mem.
                resolve_workload(setup, workload)
                setup.workloads[id] = workload
            setups.append(setup)
        return setups

    def get_updates(self) -> Dict[Setup, List]:
        updates: Dict[Setup, List] = {}
        for setup in self.setups:
            pending = []
            data = load_yaml(setup.workloads_db)
            workloads_data = data.get("workloads", {}) if data else {}
            for id, workload in setup.workloads.items():
                region_entry = workloads_data.get(id, {}) or {}
                for execution in workload.executions:
                    stored = region_entry.get(execution.executor, {}) or {}

                    # Re-estimate this executor whenever any resolved input
                    # changed (source, CPU model, compiler/flags, gem5 params,
                    # clock, or memory latency) or no valid count is stored.
                    stored_hash = stored.get("input_hash")
                    stored_cycles = stored.get("cycles_count")
                    if (not stored_hash or stored_hash != execution.input_hash
                            or not isinstance(stored_cycles, int)):
                        pending.append((workload, execution))

            if pending:
                updates[setup] = pending
        return updates

    def prepare_build_directory(self, setup: Setup, workload: Workload):
        # Create build directory
        if BUILD_DIR.exists():
            shutil.rmtree(BUILD_DIR)
        BUILD_DIR.mkdir(parents=True, exist_ok=True)

        # Copy workload .cpp file
        shutil.copy2(workload.source_path, workload.dest_path)
        
        # Copy headers and sources (except program.h/program.cpp)
        include_dir = setup.path / "include"
        src_dir = setup.path / "src"
        if include_dir.exists():
            for hdr in include_dir.glob("*.h"):
                if hdr.name == "program.h":
                    continue
                shutil.copy2(hdr, BUILD_DIR / hdr.name)
        if src_dir.exists():
            for src in src_dir.glob("*.*"):
                if src.suffix in {".c", ".cpp", ".cc", ".C"} and src.name != "program.cpp":
                    shutil.copy2(src, BUILD_DIR / src.name)

        # Create measure macros header
        measure_header_path = BUILD_DIR / MEASURE_HEADER
        measure_header_path.write_text(MEASURE_HEADER_CONTENT, encoding="utf-8")

    def prepare_workload(self, workload: Workload):
        code = workload.dest_path.read_text(encoding="utf-8")

        # Check if main function exists
        if not re.search(r"\bint\s+main\s*\(", code):
            log_error(f"{workload.source_path}: does not contain 'main()' definition. Workloads must define 'main()'.")

        # Check annotations syntax
        section_id = 0
        current_section_id = 0
        cycle_open = False
        speedup_open = False
        speedup_in_cycle = False
        has_speedup = False

        output_code = []

        for i, line in enumerate(code.splitlines(), start=1):
            # Cycle begin
            if BEGIN_CYCLE_ANNOT in line:
                if cycle_open:
                    log_error(f"{workload.source_path}: {BEGIN_CYCLE_ANNOT} on line {i} before previous CYCLE block closed.")
                cycle_open = True
                speedup_in_cycle = False
                section_id += 1
                current_section_id = section_id
                line = line.replace(BEGIN_CYCLE_ANNOT, f"BEGIN_CYCLE_MEASURE(SECTION{current_section_id})")

            # Cycle end
            elif END_CYCLE_ANNOT in line:
                if not cycle_open:
                    log_error(f"{workload.source_path}: {END_CYCLE_ANNOT} on line {i} without matching {BEGIN_CYCLE_ANNOT}.")
                if speedup_open:
                    log_error(f"{workload.source_path}: CYCLE block closed on line {i} but SPEEDUP block still open.")
                cycle_open = False
                line = line.replace(END_CYCLE_ANNOT, f"END_CYCLE_MEASURE(SECTION{current_section_id})")
                current_section_id = 0

            # Speedup begin
            elif BEGIN_SPEEDUP_ANNOT in line:
                if not cycle_open:
                    log_error(f"{workload.source_path}: {BEGIN_SPEEDUP_ANNOT} on line {i} outside of CYCLE block.")
                if speedup_open:
                    log_error(f"{workload.source_path}: Nested {BEGIN_SPEEDUP_ANNOT} on line {i}.")
                if speedup_in_cycle:
                    log_error(f"{workload.source_path}: Multiple SPEEDUP blocks in one CYCLE block on line {i}.")
                speedup_open = True
                has_speedup = True
                line = line.replace(BEGIN_SPEEDUP_ANNOT, f"BEGIN_SPEEDUP_MEASURE(SECTION{current_section_id})")

            # Speedup end
            elif END_SPEEDUP_ANNOT in line:
                if not speedup_open:
                    log_error(f"{workload.source_path}: {END_SPEEDUP_ANNOT} on line {i} without matching {BEGIN_SPEEDUP_ANNOT}.")
                speedup_open = False
                speedup_in_cycle = True
                line = line.replace(END_SPEEDUP_ANNOT, f"END_SPEEDUP_MEASURE(SECTION{current_section_id})")

            output_code.append(line)

        # Final sanity check
        if cycle_open:
            log_error(f"{workload.source_path}: CYCLE block not closed at end of file.")
        if speedup_open:
            log_error(f"{workload.source_path}: SPEEDUP block not closed at end of file.")

        # Record how many cycle regions the workload defines (used to map gem5
        # stat dumps to sections) and whether it uses a speedup region.
        workload.num_cycle_sections = section_id
        workload.has_speedup = has_speedup

        # Add include line if not present
        final_code = "\n".join(output_code)
        if MEASURE_HEADER not in final_code:
            final_code = f'#include "{MEASURE_HEADER}"\n' + final_code

        # Write back
        workload.dest_path.write_text(final_code, encoding="utf-8")

    def compile_workload(self, workload: Workload, execution: Execution):
        model = load_model(execution.model)
        compiler = model.get("compiler", RISCV_COMPILER)
        flags = list(model.get("compiler_flags", RISCV_COMPILER_FLAGS))
        abi = model.get("m5op_abi", "riscv")

        gem5_home = resolve_gem5_home()
        if not gem5_home or not gem5_home.exists():
            log_error(
                "Could not locate the gem5 source tree. Set GEM5_HOME to your "
                "gem5 checkout so the m5op header and shim can be found."
            )
        gem5_include = gem5_home / "include"
        m5op_src = gem5_home / "util" / "m5" / "src" / "abi" / abi / "m5op.S"
        if not m5op_src.exists():
            log_error(f"m5op shim for ABI '{abi}' not found: {m5op_src}")

        # Find all .c and .cpp in build directory
        src_files = []
        for ext in ("*.cpp", "*.c"):
            src_files.extend([str(src_file) for src_file in BUILD_DIR.glob(ext)])

        include_flags = ["-I.", f"-I{gem5_include}"]

        # Assemble the m5 pseudo-op shim so the region markers link.
        command = [compiler, "-c", str(m5op_src), f"-I{gem5_include}", "-o", "m5op.o"]
        proc = subprocess.run(command, cwd=str(BUILD_DIR), stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if proc.returncode != 0:
            log_error(f"m5op shim assembly failed:\n{proc.stderr}")

        # Assemble for LLVM-MCA (only needed when a speedup region is present).
        if workload.has_speedup:
            command = [compiler, "-S"] + src_files + include_flags + flags
            proc = subprocess.run(command, cwd=str(BUILD_DIR), stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            if proc.returncode != 0:
                log_error(f"Assembly compilation failed:\n{proc.stderr}")

        # Compile + link the workload for gem5.
        command = [compiler] + src_files + ["m5op.o"] + include_flags + ["-o", workload.file_id] + flags
        proc = subprocess.run(command, cwd=str(BUILD_DIR), stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if proc.returncode != 0:
            log_error(f"Binary compilation failed:\n{proc.stderr}")

    def compute_estimation(self, setup: Setup, workload: Workload, execution: Execution):
        model = load_model(execution.model)

        # Run gem5 to get per-region cycle counts. --clock (core period) and
        # --mem-latency come from the executor's resolved SystemC chiplet config.
        outdir = BUILD_DIR / "m5out"
        command = ([GEM5_BINARY, "--outdir", str(outdir), str(model_config(model)),
                    "--cmd", str(workload.binary_path)]
                   + model_cli(model, execution.params)
                   + ["--clock", execution.clock, "--mem-latency", execution.mem_latency])
        proc = subprocess.run(command, cwd=str(BUILD_DIR), stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        output = (proc.stdout or "") + "\n" + (proc.stderr or "")
        debug_output = BUILD_DIR / "debug_gem5.txt"
        debug_output.write_text(output, encoding="utf-8")
        if proc.returncode != 0:
            log_error(f"gem5 simulation failed. See {debug_output}.\n{proc.stderr}")

        # Map the per-region stat dumps to SECTION1..N cycle counts.
        cycle_sections = parse_gem5_cycles(outdir / "stats.txt", workload.num_cycle_sections)
        if not cycle_sections:
            log_error(f"No cycle sections parsed from gem5 stats. See {debug_output}.")

        # Accelerator speedup (optional; only when a SPEEDUP region is present).
        speedup_sections = {}
        if workload.has_speedup:
            if shutil.which(LLVM_ANALYZER) is None:
                log_warn(f"'{LLVM_ANALYZER}' not found; skipping accelerator speedup (factor 1.0).")
            else:
                speedup_sections = self.compute_speedup(setup, workload)

        # Compute total cycles
        total_cycles = 0
        for section_name, cycles in cycle_sections.items():
            speedup_factor = speedup_sections.get(section_name, 1.0) # default: no speedup
            total_cycles += cycles / speedup_factor

        execution.estimation_result = int(total_cycles)

    def compute_speedup(self, setup: Setup, workload: Workload) -> Dict[str, float]:
        # Run llvm-mca on the assembler file
        command = [LLVM_ANALYZER] + LLVM_ANALYZER_FLAGS + [str(workload.asm_path)]
        proc = subprocess.run(command, cwd=str(BUILD_DIR), stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        output = (proc.stdout or "") + "\n" + (proc.stderr or "")
        if proc.returncode != 0:
            log_error(f"LLVM-MCA simulation failed:\n{proc.stderr}")

        debug_output = BUILD_DIR / "debug_llvm.txt"
        debug_output.write_text(output, encoding="utf-8")

        # Parse llvm-mca output
        speedup_sections = {}

        # Split into code regions
        section_pattern = re.compile(r'\[\d+\] Code Region - (\S+)', re.MULTILINE)
        resource_pattern = re.compile(r'Resource pressure per iteration:\s*\n(?:\[.*?\]\s*\n)([0-9\.\s\-]+)', re.MULTILINE)

        for section_match in section_pattern.finditer(output):
            section_name = section_match.group(1)
            start_idx = section_match.end()
            # Grab text until next Code Region or end of file
            next_section = section_pattern.search(output, start_idx)
            end_idx = next_section.start() if next_section else len(output)
            section_text = output[start_idx:end_idx]

            # Parse resource pressure
            resource_match = resource_pattern.search(section_text)
            if not resource_match:
                log_error(f"No resource pressure info found for {section_name} in LLVM-MCA output.")

            values_line = resource_match.group(1).strip()
            resource_values = [float(v) if v != "-" else 0.0 for v in values_line.split()]

            try:
                p0 = resource_values[0]
                p1 = resource_values[1]
                p2 = resource_values[2]
                p3 = resource_values[3]
                p4 = resource_values[4]
                p5 = resource_values[5]
                p6 = resource_values[6]
            except IndexError:
                log_error(f"Resource pressure line malformed for {section_name}: {values_line}")

            # Compute speedup factor
            params = get_accel_params(setup, workload.id)

            if params["speedup_factor"]:
                speedup_factor = params["speedup_factor"]
            else:
                # Baseline bottleneck (one unit per resource)
                baseline_bottleneck = max(
                    p0,  # Integer ALU
                    p1,  # Branch
                    p2,  # FP ALU
                    p3,  # FP Div/Sqrt
                    p4,  # Integer Div
                    p5,  # Integer Mul
                    p6,  # Memory
                )

                # New bottleneck after scaling resources
                new_bottleneck = max(
                    p0 / params["num_alu"],
                    p1 / params["num_branch"],
                    p2 / params["num_fpalu"],
                    p3 / params["num_fpdivsqrt"],
                    p4 / params["num_idiv"],
                    p5 / params["num_imul"],
                    p6 / params["num_mem"],
                )

                speedup_factor = baseline_bottleneck / new_bottleneck

            speedup_sections[section_name] = speedup_factor

        return speedup_sections

    def run(self):
        if not self.updates:
            log_info("All workloads up-to-date.")
            return
        for setup, pending in self.updates.items():
            # Executions that share an input hash (same source, model, params,
            # clock, and memory latency) yield the same count; compute once.
            computed: Dict[str, int] = {}
            for workload, execution in pending:
                if execution.input_hash in computed:
                    execution.estimation_result = computed[execution.input_hash]
                else:
                    log_info(f"Updating cycle estimation for '{workload.source_path}' "
                             f"on {execution.executor}...")
                    self.prepare_build_directory(setup, workload)
                    self.prepare_workload(workload)
                    self.compile_workload(workload, execution)
                    self.compute_estimation(setup, workload, execution)
                    computed[execution.input_hash] = execution.estimation_result
                self.update_database(setup, workload, execution)

def main():
    # gem5 is the one hard requirement; the cross-compiler is validated per
    # workload (from its model), and llvm-mca only when a speedup region exists.
    if shutil.which(GEM5_BINARY) is None and not Path(GEM5_BINARY).exists():
        log_warn(f"gem5 binary '{GEM5_BINARY}' not found (set GEM5_BIN). Skipping cycle estimation.")
        sys.exit(EXIT_SKIPPED)
    if not resolve_gem5_home() or not resolve_gem5_home().exists():
        log_warn("gem5 source tree not found (set GEM5_HOME). Skipping cycle estimation.")
        sys.exit(EXIT_SKIPPED)

    cycle_estimator = CycleEstimator()
    cycle_estimator.run()

if __name__ == "__main__":
    main()