# Writing Program Code

Each setup describes *what hardware exists* in `system.yaml` and *what that
hardware does* in `src/program.cpp`. This document explains how to write the
program code: the module entry points, the AXI and DMA transfer APIs, and how to
model per-workload compute time with the cycle-estimation tool.

## The entry point

`program.cpp` must define one function:

```cpp
extern "C" ModuleCodeMap *get_program_code();
```

It returns a map from a *module key* to the code that runs on that module:

```cpp
ModuleCodeMap *get_program_code() {
    static ModuleCodeMap code = {
        {{"chiplet0", "core0"}, {CPUCode{ .main = ..., .irq = ... }}},
        {{"chiplet1", "dfp"}, {AccelCode{ .main = ... }}},
    };
    return &code;
}
```

- The key is `{"<chiplet>", "<module>"}` and must match the names in
  `system.yaml`.
- A compute core uses the module name `core0` (or `core1`, `core2`, ... on a
  multi-core chiplet) and takes a `CPUCode`.
- An accelerator uses its own name as the module and takes an `AccelCode`.

Provide an entry for every accelerator declared in `system.yaml`. Cores without
an entry simply stay idle.

## Core programs (`CPUCode`)

```cpp
struct CPUCode {
    std::function<void(Core &)>              main;
    std::function<void(Core &, const IRQ &)> irq;
};
```

- `main` runs once when the simulation starts. Use it to kick off the work for
  that core.
- `irq` runs every time the core receives an interrupt. The `IRQ` argument
  carries the context of the event:

```cpp
struct IRQ {
    uint32_t request_id;     // tag supplied by the sender
    uint8_t  target_module;
    uint32_t target_address; // address associated with the interrupt
    uint8_t  burst;
    unsigned data_length;    // payload size in bytes
};
```

Call `sc_stop()` from either handler to end the simulation.

```cpp
{{"chiplet0", "core0"},
 {CPUCode{
     .main = [](Core &core) {
         auto *buf = new unsigned char[64];
         auto  req = AxiRequest(0, buf, 64)
                         .to_via("chiplet0", "memory", "interconnect");
         core.write(req)->wait();
         delete[] buf;
     },
     .irq = [](Core &core, const IRQ &irq) {
         // react to an incoming interrupt
     }}}}
```

## Accelerator programs (`AccelCode`)

```cpp
struct AccelCode {
    std::function<void(HWAccel &, uint8_t *data, size_t size)> main;
};
```

`main` runs when the accelerator receives a block of data. `data`/`size` is that
input buffer; process it in place and write the result back out.

```cpp
{{"chiplet1", "dfp"},
 {AccelCode{ .main = [](HWAccel &accel, uint8_t *data, size_t size) {
     for (size_t i = 0; i < size; ++i) {
         data[i] = transform(data[i]);
     }
     accel.wait_cycles("matalu");            // model compute time
     auto req = AxiRequest(4, data, size);   // send the result on
     accel.write(req)->wait();
 }}}}
```

## AXI API

Cores and accelerators both expose `read` and `write`; cores additionally expose
`dma` (see below). Each returns a `std::shared_ptr<RequestHandle>`.

```cpp
std::shared_ptr<RequestHandle> read(const AxiRequest &req);
std::shared_ptr<RequestHandle> write(const AxiRequest &req);
```

### Building a request

Construct an `AxiRequest` with a request id, a data buffer, and a length, then
chain the builder methods you need:

```cpp
auto req = AxiRequest(request_id, buffer, length)
               .set_addr(0x1000);
```

| Method | Effect |
|--------|--------|
| `AxiRequest(id, data, len)` | `id` is a caller-chosen tag; `data`/`len` is the buffer (source bytes for a write, destination for a read). |
| `.set_addr(addr)` | Destination address. Required for reads; optional for writes. |
| `.to(module)` | Issue through the named local module (default `memory`). |
| `.to_via(chiplet, module, via)` | Target a specific `chiplet`/`module`, leaving through the local interconnect `via`. |
| `.set_burst(type)` | `ARM::AXI::BURST_INCR` (default), `BURST_FIXED`, or `BURST_WRAP`. |
| `.use_ext(id)` | Attach a `SmartExtension::ID` (e.g. a crypto extension) to the transfer. |

If no destination is given, the request stays on the caller's own chiplet and
targets its `memory` module.

You own the data buffer: allocate it before the call and free it after the
transfer completes.

### Waiting for completion

`read`/`write`/`dma` are non-blocking; they return a handle you wait on:

```cpp
auto handle = core.read(req);
handle->wait();          // suspends this process until the transfer finishes
// for a read, `buffer` now holds the result
```

`wait()` returns immediately if the transfer already completed. For reads, the
buffer passed in the request is filled once the handle completes.

## DMA API

A core can move data directly between two modules without staging it in the
core itself:

```cpp
std::shared_ptr<RequestHandle> dma(const AxiDMARequest &req);
```

Build the request with a source (`from`) and a destination (`to`):

```cpp
auto dma = AxiDMARequest(request_id, length)
               .from("chiplet1", "memory", 0x0)
               .to("chiplet1", "dfp", 0x0);
core.dma(dma);
```

| Method | Effect |
|--------|--------|
| `AxiDMARequest(id, len)` | Request tag and transfer size in bytes. |
| `.from(chiplet, module, addr)` | Source module and address. |
| `.from_via(chiplet, module, addr, via)` | Source, leaving through local interconnect `via`. |
| `.to(chiplet, module, addr)` | Destination module and address. |
| `.to_via(chiplet, module, addr, via)` | Destination, reached through local interconnect `via`. |
| `.set_burst(type)` | Burst type, as for `AxiRequest`. |
| `.use_ext(id)` | Attach a `SmartExtension::ID`. |

Use `from_via`/`to_via` when the transfer crosses a chiplet boundary and you
need to name the interconnect it travels over.

## Modeling compute time

Program code models *communication* explicitly (the AXI/DMA transfers above).
*Computation* is modeled by advancing simulation time for a named workload:

```cpp
core.wait_cycles("matmul");    // or accel.wait_cycles("matmul")
```

This suspends the caller for the number of clock cycles associated with the
workload `matmul`, scaled by the core's clock period (so a chiplet with a
non-default `cores.clk_cycle` advances time accordingly). Cycle counts live in
the setup's `workloads.yaml` and are produced by the cycle-estimation tool.

To wait a fixed number of cycles without an estimation entry, pass a count
directly:

```cpp
core.wait_cycles(500);
```

### Memory-latency boundary

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
is acceptable for the single-core setups here and is common-mode across design
points.

### The cycle-estimation workflow

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

The estimator re-measures a workload when its source or any resolved gem5 input
changes (the CPU model, compiler, parameters, core clock, or memory latency),
and it skips silently if gem5 is not installed. Accelerator speedup
additionally needs `llvm-mca`; compiling workloads needs the RISC-V toolchain.

### Modeling accelerator speedup (optional)

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

### CPU model and gem5 parameters

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

Two SystemC config values are also passed to gem5: the core clock period
(`cores.clk_cycle`) becomes gem5's CPU clock, and the local memory latency is
`memory.access_latency` x `memory.clk_cycle`. They are marked `GEM5` in the
default configs; changing them, a gem5 parameter, the model, or the compiler
invalidates the cached estimate so the next build re-runs gem5. Values marked
`DO NOT EDIT` (e.g. `cores.num`) are structural and hidden from the editor.

To add a model, drop a manifest in the models directory declaring its
`cpu_type`, `compiler`, `m5op_abi`, and `params`; the gem5 build must include
that ISA. A custom config script must accept and use `--clock` and
`--mem-latency` (see `se_model.py` for the reference).
