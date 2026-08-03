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
        {{"fpga", "core0"}, {CPUCode{ .main = ..., .irq = ... }}},
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
{{"fpga", "core0"},
 {CPUCode{
     .main = [](Core &core) {
         auto *buf = new unsigned char[64];
         auto  req = AxiRequest(0, buf, 64)
                         .to_via("chiplet0", "memory", "interconnect")
                         .skip_cache();
         core.write(req)->wait();
         delete[] buf;
     },
     .irq = [](Core &core, const IRQ &irq) {
         // react to an incoming interrupt; call sc_stop() when done
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
               .set_addr(0x1000)
               .skip_cache();
```

| Method | Effect |
|--------|--------|
| `AxiRequest(id, data, len)` | `id` is a caller-chosen tag; `data`/`len` is the buffer (source bytes for a write, destination for a read). |
| `.set_addr(addr)` | Destination address. Required for reads; optional for writes. |
| `.to(module)` | Issue through the named local module (default `memory`). |
| `.to_via(chiplet, module, via)` | Target a specific `chiplet`/`module`, leaving through the local interconnect `via`. |
| `.set_burst(type)` | `ARM::AXI::BURST_INCR` (default), `BURST_FIXED`, or `BURST_WRAP`. |
| `.skip_cache()` | Bypass the cache (volatile access). |
| `.use_ext(id)` | Attach a `SmartExtension::ID` (e.g. a crypto extension) to the transfer. |

If no destination is given, the request stays on the caller's own chiplet and
targets its `memory` module. Cross-chiplet and unaddressed writes are treated as
volatile automatically.

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
| `.skip_cache()` | Bypass the cache. |
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
workload `matmul`. Cycle counts live in the setup's `workloads.yaml` and are
produced by the cycle-estimation tool.

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
   the simulation), or
   run it directly with `tools/cycle_estimation/main.py`. It compiles the
   workload for RISC-V, measures cycles with Spike, and records the result in
   `workloads.yaml` under a key equal to the file name (`<workload>`).
4. Reference that name from `program.cpp`: `wait_cycles("<workload>")`.

The estimator re-measures a workload only when its source changes (it tracks a
hash), and it skips silently if the RISC-V toolchain (`riscv64-unknown-elf-gcc`,
`spike`, `llvm-mca`) is not installed.

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
