# Cycle Estimation

Program code models *communication* explicitly with the AXI/DMA APIs (see
[PROGRAM-CODE.md](PROGRAM-CODE.md)). *Computation* is modeled by advancing
simulation time for a named workload through `wait_cycles("<workload>")`. The
cycle counts that call consumes are produced by the cycle estimator: it
compiles each workload, runs it under a gem5 CPU model, and records the measured
per-region cycles in the setup's `workloads.yaml`.

Estimation runs automatically - the CLI runs it before every `make run`, and the
GUI before every simulation run - but it only invokes gem5 when a workload
actually needs re-measuring. A workload is skipped when its cached count is
still valid (nothing that affects the result has changed since it was recorded;
see [The cycle estimation workflow](#the-cycle-estimation-workflow)), so a run
whose counts are up to date starts the simulation immediately. It is also
skipped, silently and for every workload, when its tools are missing, leaving
the existing cycle counts in place. It runs in the bundled application too (the
estimator ships as a standalone executable), so a bundle install estimates
cycles whenever gem5 and the workload compiler are on `PATH`. The tools and how
they are discovered are listed in the
[Cycle estimation tools](../README.md#cycle-estimation-tools) section of the
README.

## The memory-latency boundary

Every memory access is charged in exactly one place, so latency is never
double-counted:

- **Inside `wait_cycles` (gem5):** the compute kernel's pipeline timing and its
  accesses to its local working buffers through the L1 cache. `cycles_count` is
  therefore the compute time on data already resident locally.
- **In explicit API calls (SystemC):** moving data into and out of local
  buffers and to and from remote chiplets (`read`/`write`/`dma`) carries the
  fabric and memory latency.

So a workload's own loads and stores are gem5's job and must not be duplicated
with API calls: `program.cpp` moves the data with API calls, then calls
`wait_cycles` for the compute. When it needs to stage a kernel's local buffers
whose access cost is already in the `wait_cycles` estimate, it can use the free
local memory access (`local_read`/`local_write`, see
[PROGRAM-CODE.md](PROGRAM-CODE.md)) instead of a costed AXI transfer. Contention
from simultaneous compute on multiple cores is not modeled (only contention on
the explicit AXI/DMA traffic is); this is an acceptable trade-off and is
common-mode across design points.

## The cycle estimation workflow

1. Write a small, self-contained C++ program in
   `setups/<name>/workloads/<workload>.cpp` with an `int main()`. It should
   perform the same computation the real module does.
2. Mark the region to measure:

   ```cpp
   //@BEGIN_CYCLE_MEASURE
   for (size_t i = header; i < total; ++i) {
       out[i] = 255 - in[i];
   }
   //@END_CYCLE_MEASURE
   ```

3. Declare which modules run the workload in a sidecar next to the source,
   `setups/<name>/workloads/<workload>.yaml`:

   ```yaml
   executors:
     - chiplet0.core0  # chiplet.module entries; list every module that runs it
   ```

   The estimator resolves each executor's CPU model, gem5 parameters, core
   clock, and memory latency from that chiplet, and produces one cycle count per
   executor. The declaration is authoritative: the estimator does not infer
   executors from `program.cpp`. A missing or empty declaration is an error.
4. Run `make run ARGS="--setup=<name>"` (the estimator runs automatically before
   the simulation), or run it directly with `tools/cycle_estimation/main.py`. It
   compiles the workload with the owning chiplet's CPU-model compiler, runs it
   under gem5 to obtain per-region cycle counts, and records them in
   `workloads.yaml` keyed by the region name and each declared executor (so the
   same kernel on different cores can differ).
5. Reference that name from `program.cpp`: `wait_cycles("<workload>")`, from the
   same modules you declared as executors.

In the GUI you only author the workload, its executor declaration, and the
`wait_cycles` reference (steps 1, 2, 3, and 5); each run then builds the setup,
estimates cycles, and simulates automatically, so there is no separate
estimation command to invoke.

The estimator re-measures a workload when its source or any resolved gem5 input
changes (the CPU model, compiler, parameters, core clock, or memory latency),
and it skips silently if gem5 is not installed. Accelerator speedup additionally
needs `llvm-mca`; compiling workloads needs the CPU model's compiler.

## Modeling accelerator speedup (optional)

To model an accelerator that executes a region faster than a scalar core, wrap
that region with a speedup annotation *inside* the cycle block:

```cpp
//@BEGIN_CYCLE_MEASURE
//@BEGIN_SPEEDUP_MEASURE
    heavy_alu_loop();
//@END_SPEEDUP_MEASURE
//@END_CYCLE_MEASURE
```

The estimator analyzes the instruction mix of that region and scales its cycles
by the resources declared in the accelerator's config
(`configs/accelerators/<type>.yaml`): `num_alu`, `num_branch`, `num_fpalu`,
`num_fpdivsqrt`, `num_idiv`, `num_imul`, `num_mem`. Alternatively, set an
explicit `speedup_factor` in that config to override the analysis. The
accelerator instance is taken from the workload's declared executors: each
`chiplet.module` that names an accelerator has its speedup applied from that
accelerator's config.

## CPU model and gem5 parameters

Each chiplet that has cores estimates cycles with a gem5 CPU model, defaulting
to `riscv-minor` (an in-order RISC-V pipeline with private L1 caches). Select a
different model or override its parameters per chiplet with a `gem5:` block in
`system.yaml` (the GUI's setup editor exposes this for core-bearing chiplets):

```yaml
chiplets:
  - name: chiplet0
    type: compute
    gem5:
      cpu_model: riscv-minor
      params:
        l1d-size: 64kB
```

Three SystemC config values are fed to gem5: the core clock period
(`cores.clk_cycle`) becomes gem5's CPU clock, and the memory clock period
(`memory.clk_cycle`) together with the access latency in cycles
(`memory.access_latency`) give the backing-memory latency, passed as
`memory.access_latency` x `memory.clk_cycle`. They are marked `GEM5` in the
default configs; changing any of them, a gem5 parameter, the model, or the
compiler invalidates the cached estimate so the next run re-runs gem5.

To add a model, provide a manifest and a gem5 config script (the manifest may
reuse the bundled `se_model.py` instead of shipping its own).

The manifest (`<model>.yaml`) declares:

- `name` - the model name, shown in the editor and referenced by `cpu_model`.
- `description` - optional description shown in the editor.
- `config` - the gem5 config script to run (defaults to `se_model.py`).
- `cpu_type` - the gem5 CPU class, e.g. `RiscvMinorCPU`.
- `mem_mode` - optional memory simulation mode.
- `m5op_abi` - the m5op shim ABI to assemble, e.g. `riscv` or `arm64`.
- `compiler` and optional `compiler_flags` - the workload cross-compiler.
- `params` - the model-specific gem5 parameters it exposes, with defaults;
  these appear in the editor's gem5 block and are passed to the config script.

The gem5 build must include the model's ISA. The config script is run as
`gem5 <config.py> --cmd <workload-elf> --cpu-type <type> [--mem-mode <mode>]
--<param> <value> ... --clock <core-period> --mem-latency <latency>`: it
receives `--cmd`, `--cpu-type` (and `--mem-mode` when the manifest sets it), one
`--<param>` flag per declared parameter, and `--clock`/`--mem-latency` derived
from the SystemC config. It must build the gem5 system from those arguments and
dump per-region stats; use `se_model.py` as the reference.

Place the files by install type. From source, put the manifest (and any custom
script) in `tools/cycle_estimation/gem5/models/`. In the bundled application,
put them in `~/.local/share/chiplex/gem5-models/` (created on first launch);
this directory is searched before the bundled models, so a manifest there also
overrides a bundled model of the same name. A config script placed next to its
manifest is found automatically. The manifest file name must be the model name
with dashes as underscores (e.g. `riscv-minor` -> `riscv_minor.yaml`).
