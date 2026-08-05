# Cycle Estimation

Program code models *communication* explicitly with the AXI/DMA APIs (see
[PROGRAM-CODE.md](PROGRAM-CODE.md)). *Computation* is modeled by advancing
simulation time for a named workload through `wait_cycles("<workload>")`. The
cycle counts that call consumes are produced by the cycle-estimation tool: it
compiles each workload, runs it under a gem5 CPU model, and records the measured
per-region cycles in the setup's `workloads.yaml`.

Estimation runs automatically - the CLI runs it before every `make run`, and the
GUI before every run - and skips silently when its tools are missing, leaving
the existing cycle counts in place. The tools and how they are discovered are
listed in the [Cycle-estimation tools](../README.md#cycle-estimation-tools)
section of the README.

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
`wait_cycles` for the compute. Contention from simultaneous compute on multiple
cores is not modeled (only contention on the explicit DMA/AXI traffic is); this
is an acceptable trade-off and is common-mode across design points.

## The cycle-estimation workflow

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

3. Run `make run ARGS="--setup=<name>"` (the estimator runs automatically before
   the simulation), or run it directly with `tools/cycle_estimation/main.py`. It
   compiles the workload with the owning chiplet's CPU-model compiler, runs it
   under gem5 to obtain per-region cycle counts, and records them in
   `workloads.yaml` keyed by the region name and the chiplet and core that run
   it (so the same kernel on different cores can differ).
4. Reference that name from `program.cpp`: `wait_cycles("<workload>")`.

In the GUI you only author the workload and reference it (steps 1, 2, and 4);
each run then builds the setup, estimates cycles, and simulates automatically,
so there is no separate estimation command to invoke.

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
accelerator is identified by matching the `wait_cycles("<workload>")` call in its
`AccelCode`.

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

Models live in `tools/cycle_estimation/gem5/models/*.yaml`; each names a gem5
config script, a compiler, the m5op ABI, and the parameters it exposes with
defaults. These parameters are model-specific and are not part of a chiplet's
default config.

Three SystemC config values are also fed to gem5: the core clock period
(`cores.clk_cycle`) becomes gem5's CPU clock, and the memory clock period
(`memory.clk_cycle`) together with the access latency in cycles
(`memory.access_latency`) give the backing-memory latency, passed as
`memory.access_latency` x `memory.clk_cycle`. They are marked `GEM5` in the
default configs; changing any of them, a gem5 parameter, the model, or the
compiler invalidates the cached estimate so the next run re-runs gem5.

To add a model, drop a manifest in the models directory declaring its
`cpu_type`, `compiler`, `m5op_abi`, and `params`; the gem5 build must include
that ISA. A custom config script must accept and use `--clock` and
`--mem-latency` (see `se_model.py` for the reference).
