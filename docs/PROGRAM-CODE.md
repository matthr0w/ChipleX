# Writing Program Code

Each setup describes *what hardware exists* in `system.yaml` and *what that
hardware does* in `src/program.cpp`. This document explains how to write the
program code: the module entry points and the AXI and DMA transfer APIs.
Modeling per-workload compute time is covered separately in
[CYCLE-ESTIMATION.md](CYCLE-ESTIMATION.md).

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

An entry is optional for every module. A core or accelerator with no entry
simply stays idle.

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
    uint32_t request_id;      // tag supplied by the sender
    uint8_t  target_module;
    uint32_t target_address;  // address associated with the interrupt
    uint8_t  burst;
    unsigned data_length;     // payload size in bytes
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
     accel.wait_cycles("matalu");           // model compute time
     auto req = AxiRequest(4, data, size);  // send the result on
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
| `.set_addr(addr)` | Destination address. Required for reads; optional for writes - if omitted, the target module allocates a free address dynamically. |
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
handle->wait();  // suspends this process until the transfer finishes
// for a read, `buffer` now holds the result
```

`wait()` returns immediately if the transfer already completed. For reads, the
buffer passed in the request is filled once the handle completes.

## Local memory access

A core can read and write its own chiplet's memory directly, without generating
bus traffic and without advancing simulated time:

```cpp
uint32_t local_write(const void *data, unsigned length, std::optional<uint32_t> address = std::nullopt);
void     local_read(void *data, unsigned length, uint32_t address);
```

Use this for data whose access cost is already accounted for elsewhere - most
often a compute kernel's accesses to its local working buffers, which the
kernel's `wait_cycles` estimate already includes (see the memory-latency
boundary in [CYCLE-ESTIMATION.md](CYCLE-ESTIMATION.md)). Staging that data with
the AXI API instead would double-count the local access.

```cpp
uint32_t addr = core.local_write(input, length);  // dynamic address, returned
core.wait_cycles("kernel");                       // kernel works on the buffer
core.local_read(output, length, addr);            // retrieve results
```

Addressing and lifetime mirror the AXI path: `local_write` without an address
allocates a free range and returns it, or writes to the address you pass;
`local_read` frees the range it consumes. Both calls are synchronous and
complete immediately - there is no handle to wait on.

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
core.wait_cycles("matmul");  // or accel.wait_cycles("matmul")
```

This suspends the caller for the number of clock cycles associated with the
workload `matmul`, scaled by the core's clock period (so a chiplet with a
non-default `cores.clk_cycle` advances time accordingly). Cycle counts live in
the setup's `workloads.yaml` and are produced by the cycle estimator.

To wait a fixed number of cycles without an estimation entry, pass a count
directly:

```cpp
core.wait_cycles(500);
```

A workload's own loads and stores are already charged inside `wait_cycles`, so
do not move that data with API calls as well. See
[CYCLE-ESTIMATION.md](CYCLE-ESTIMATION.md) for how counts are produced, the
memory-latency boundary, accelerator speedup, and CPU-model selection.
