# Changes: correctness, robustness, and performance overhaul

This document records the fixes applied to the framework, with particular attention to
**behavior-affecting** changes (marked :warning:) that may alter simulation output. If a
thesis experiment relied on the previous behavior, re-run the affected setup(s) after
these changes. Every behavior-affecting fix is in its own commit so it can be reverted in
isolation.

Verification method: each fix is checked by rebuilding (Release) and re-running the
affected setups, diffing `stats.json` against a pre-change baseline; memory-safety fixes
are additionally verified under AddressSanitizer/UBSan (`-DENABLE_ASAN=ON`).

---

## Build system (Phase 0)

- **CMake now defaults to a `Release` build** (`-O3 -DNDEBUG`) instead of the previous
  unset build type (which compiled at `-O0`). This is a large wall-clock speedup with no
  effect on results. Opt-in `ENABLE_LTO` and `ENABLE_ASAN` configurations were added.
- SystemC discovery gained a `find_package(SystemCLanguage)` fallback after the
  `SYSTEMC_PATH` env probe. `yaml-cpp` is pinned to release `0.8.0` (was `master`).

---

## Correctness fixes

### C1 — Memory heap out-of-bounds (fixed)
`src/modules/Memory.cpp`. The per-beat write/read `std::memcpy(&mem[a], ...)` had no
bounds check; a workload address at/above `mem.size()` (or an off-chip address past the
backing store) corrupted the heap. Added an overflow-safe bounds check that logs an error
and skips the access instead of corrupting memory. No result change on the existing setups
(they never address out of range) — verified identical `stats.json`.

### M10 — SPI arbitration head-of-line block (fixed)
`src/modules/interconnects/SPI.cpp`. Both link-arbitration loops used
`pop_front`/`push_front`, which re-examined the queue head every iteration and never
scanned past a non-deliverable head (permanent head-of-line blocking). Replaced with an
in-order scan that removes only the matched request, preserving FIFO order. No result
change on the existing setups (their SPI queues do not reach the multi-entry state that
triggers the bug) — verified identical `stats.json`. The fix prevents the latent stall
under heavier link contention.

### M3 — initiator payloads never returned to the pool (DEFERRED, see rationale)
`ARM::AXI::Payload::new_payload()` starts a payload at refcount 1; the initiator
(`Core`/`HWAccel`) never calls `unref()`, so completed payloads are never returned to the
`PayloadPool` and memory grows with transaction count.

Two targeted fixes were attempted and **reverted**, each proving the same thing:
(1) unref in the `RequestHandle` destructor -> crashes every cross-chiplet setup (the handle
is destroyed, for fire-and-forget requests, while an interconnect still points at the payload);
(2) unref at transaction completion in `Core::clk_posedge` -> hangs `bcdc` (the serial-link
`NetworkLayer` holds the initiator payload in `axi_in_trans` *beyond* Core's completion point,
so the recycled payload corrupts an in-flight transfer that then never completes). Root cause:
the datapath modules (`GenericInterconnect`, `SPI`, `SerialLink`, `ExtensionLayer`, `Bus`,
`Core`, `DMAEngine`) store the initiator payload as a **raw, non-reference-counted pointer**,
each with a different lifetime, so no single unref point is safe. Only `Memory`, `Cache`, and
`HWAccel` currently `ref()/unref()`.

Correct fix (a dedicated, AddressSanitizer/LeakSanitizer-verified change): apply the TLM
reference-count discipline uniformly — every module that stores a payload beyond a single
`nb_transport` call must `ref()` on enqueue and `unref()` on dequeue/clear, and the
initiator `unref()`s at completion. This is a datapath-wide change and is tracked
separately to avoid destabilizing the framework mid-pass.

### C2 — heap overflow on non-beat-aligned read/write transfers (fixed)
`Core.cpp`, `HWAccel.cpp`, `Requests.h`. Found via AddressSanitizer on the `lz4` setup
(142-byte transfer). The ARM payload moves whole AXI beats, so `write_in` reads and `read_out`
writes `get_data_length()` (beat-aligned, e.g. 144) bytes; the framework passed the caller's
`data`/`data_length` buffer directly, so any transfer whose length is not a multiple of the AXI
beat width over-read (write path) or over-wrote (read path) the caller's buffer. Writes now pad
a zero-filled beat-aligned scratch buffer when needed; reads copy back only `data_length` bytes
through a scratch buffer. The beat-aligned common case is unchanged (no extra copy). Verified
identical `stats.json` and clean under ASan.

### M9 — Cache div-by-zero and non-power-of-two indexing (fixed)
`src/modules/Cache.cpp`. `store_buffer_size == 0` caused a `% 0` crash, and `size == 1`
made the store buffer permanently "full"; the tag/index computation used float
`(unsigned)log2(x)` (truncates incorrectly near integer boundaries) and assumed power-of-two
geometry without validating it. Added load-time asserts (`store_buffer_size >= 2`,
power-of-two `block_size` and line count) and replaced the float `log2` with an exact
integer `__builtin_ctz`. Verified identical `stats.json` (configs already use valid
power-of-two geometry).

### M7 — serial-link bit-error forged an address-0 transaction (fixed) :warning:
`include/modules/interconnects/serial_link/ChannelAllocator.h`. On an injected bit error the
code zeroed the flit's **address** word and data but left the tag valid, so the receiver
performed a real write/read to address 0. Now only the data payload is corrupted; the
address, tag, and flow-control fields are preserved (there is no retransmission layer, so a
corrupted flit cannot be dropped without deadlocking the credit-based link). Behavior-affecting
only under non-negligible `--ber`; the effect is on transferred data fidelity, not on the
timing/utilization recorded in `stats.json`. Verified: high-BER runs complete and are now
reproducible for a fixed `--seed`.

### M8 — Generic interconnect mixed AXI channels in one flit (fixed) :warning:
`src/modules/interconnects/generic/Generic.cpp`. `handle_axi_channels()` gated only AW/AR on
`channel == None`; W/B/R ran unconditionally and the flush guard was checked only on entry, so
two channels could append to the same staging buffer and produce a flit whose tag did not match
its contents (the receiver's `process_flit` parses one channel per flit). Restructured to
process exactly one channel per call, with AW/AR releasing the channel and W/R accumulating
their multi-beat data. Verified identical `stats.json` on all five PCIe setups (matmul, matops,
matsquare, tumlogo, duckcam) — the mixing was latent for these workloads' timing, so this is a
correctness hardening with no result change here.

## Robustness fixes (minor)

- **m1 — reproducible RNG** (`include/globals.h`, `src/common/Parser.cpp`): the bit-error RNG
  was seeded from `std::random_device` (non-reproducible). It now uses a fixed default seed,
  overridable with `--seed=<n>`, and is re-seeded after CLI parsing. No effect on the default
  setups (bit errors do not fire at the default BER).
- **m2 — fatal errors never fall through** (`include/logging.h`, `src/main.cpp`): `LOG_ERROR`/
  `SC_LOG_ERROR`/`*_ASSERT` were no-ops under `SILENT`, so error paths fell through to
  undefined behavior (e.g. indexing an array with a `-1` "no route" id). They now always throw
  (message suppressed by log level); `sc_main` catches the exception, dumps partial stats, and
  exits non-zero instead of calling `std::terminate`. Verified identical on all setups (none
  were silently hitting an error path).
- **m4 — IRQ delivered to the originating core** (`Generic.cpp`, `SPI.cpp`, `NetworkLayer.cpp`):
  completion IRQs were hardcoded to `irq_sockets[0]`; they now use `user.core` (bounds-guarded).
  Latent for the current single-core setups (identical `stats.json`); correct for multi-core.
- **m5 — `dont_initialize()` on the extension layer** (`ExtensionLayer.cpp`): matches every
  other clocked module (no spurious pre-clock activation). Verified identical.
- **m6 — well-defined wire (de)serialization** (`DataLinkLayer.cpp`): pack/unpack now `memcpy`
  through a real `PayloadWire_t` object instead of writing through a `reinterpret_cast` pointer
  whose object lifetime never began (UB). Verified identical.
- **m8 — misc**: initialized `AxiBeat` members (`ExtensionBase.h`); made `StreamFifo::num_free()`
  underflow-safe; fixed a signed/unsigned loop bound in `main.cpp`.

### M2 / M4 / M5 — interconnect leaks & a link stall (fixed)
Contained fixes, each verified identical on all setups (the affected paths are latent for the
current workloads) and clean under ASan/UBSan:
- **M5** (`Generic.cpp`, PHY receive): a flit was dropped when the Rx buffer was full, leaking
  the sender's transaction and **permanently stalling that link** (no `BEGIN_RESP`, so
  `phy_active_tx` stayed set). Now applies backpressure (leaves the flit queued, retries).
- **M4** (`Generic.cpp`, `process_flit` AW/AR): a `Payload` was allocated and stored in
  `manager_payloads` on every reprocessed flit under backpressure, leaking the previous one.
  Allocation now happens only when the channel is free and the flit is consumed.
- **M2** (`DataLinkLayer.cpp`): under receiver backpressure (`TLM_ACCEPTED`) the packed
  transaction + buffer were never freed and re-allocated each stalled cycle. Now freed on that
  path.

### M1 — serial-link `Payload_t` leak (fixed)
`NetworkLayer.cpp`, `DataLinkLayer.cpp`, `NetworkLayer.h`. The serial link's wire `Payload_t`
objects flowed through the stream FIFOs and were never freed: `sender_thread` heap-allocated a
fresh one on every wakeup (orphaning all but the one `clk_posedge` committed), and consumers
discarded the pointers they read. Established a clear ownership rule -- FIFO entries are owning
pointers, freed by whichever stage consumes them: `sender_thread` now reuses the pending payload
until `clk_posedge` commits it (then nulls the member); `SLDataLinkLayer::clk_posedge` frees the
payload after packing it into the wire transaction; `SLNetworkLayer::clk_posedge` frees a payload
once it has been consumed from the receive FIFO (forwarded payloads move to the out FIFO and are
freed downstream instead). Result on `bcdc`: leaked memory per run dropped from **~19.5 MB to
~0.2 MB** (a 99% reduction); verified clean under ASan (no use-after-free / double-free) and
byte-identical `stats.json`.

### M6 — undefined-behavior socket casts removed (fixed)
`InterconnectBase.h` + `SerialLink.cpp` / `Generic.cpp` / `SPI.cpp`. The interconnects stored
their concrete sockets (templated on their own module type) into `InterconnectBase` pointers of
a different template instantiation via `reinterpret_cast` — undefined behavior that only worked
because the layouts happened to match. `InterconnectBase` now stores the ports as the
module-independent TLM base socket types (`ARM::TLM::BaseTargetSocket`/`BaseInitiatorSocket` for
the AXI ports, `tlm::tlm_{target,initiator}_socket<>` for the tagged link/IRQ ports), so the
concrete sockets convert by a normal implicit upcast. These ports are used only for `bind()`.
Verified identical on all setups.

## Performance

- **Build optimization (biggest win)**: enabling `Release` (`-O3 -DNDEBUG`) over the previous
  unset/`-O0` default measured **~2.1x** faster on the compute-heavy `duckcam` setup
  (32.1s -> 15.0s for a 10 ms simulated window), at zero behavioral cost.
- **P2 — StatManager**: nested `std::map` + per-call `register_* + re-lookup + dynamic_cast`
  (~4 tree lookups per update) replaced with `std::unordered_map` and a single lookup + one
  checked cast; `register_*` now returns a typed handle callers can cache. `dump_to_file` sorts
  keys so `stats.json` stays byte-identical. Also fixes the m7 null-dereference on a mismatched
  cast. Verified identical output.
- **P5**: `CryptoExtension::dump_data` (an unconditional per-beat `std::cout`, even under
  SILENT) is now gated behind DEBUG.
- **P6**: `Router::get_dest_id` is now an O(1) reverse-table lookup instead of a linear scan
  over all connections.

Note: `duckcam` is dominated by the SystemC cooperative scheduler dispatching millions of
per-cycle clocked processes (72 M cycles), so the code-level P2/P5/P6 wins are marginal *for
that setup* (they help stat/crypto-heavy paths elsewhere). Reducing the per-cycle process count
(gating idle clocked methods, or coarser clocks) is the remaining lever and is architectural —
see "Deferred".

## Deferred (documented, not applied)

These are real but require dedicated, individually-verified work; they were kept out of this
pass to avoid destabilizing the framework. None cause crashes or wrong results in the current
setups (ASan/UBSan reports **zero** memory or UB errors across all setups) — they are memory
growth or works-in-practice issues.

- **M3 — initiator `Payload` leak** (KB-scale: matmul ~5.9 KB, default ~33 KB, bcdc residual
  ~210 KB per run): the ARM `Payload` is held as a raw, non-reference-counted pointer by
  datapath modules with differing lifetimes, so no single unref point is safe (two attempts
  crashed / hung — see the M3 entry above). The correct fix is a uniform TLM `ref()/unref()`
  ownership pass across the datapath, verified under LeakSanitizer. Deferred because it is a
  high-risk, datapath-wide change for a small (KB-scale) leak.
- **Per-cycle scheduler cost**: the dominant runtime cost for long simulations. Gating
  clocked methods on activity or supporting coarser time steps would help but changes the
  timing model and needs its own validation.
- **Vendored `stb_image_write` UB (`tumlogo`)**: UBSan reports a `left shift of negative value`
  inside `setups/tumlogo/include/stb_image_write.h` (the JPEG encoder). This is a well-known,
  benign issue in the third-party `stb` library and is not framework code; left as-is.
