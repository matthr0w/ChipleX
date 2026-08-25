## Parallel Matrix Multiplication

`C = A x B`, both `MATRIX_SIZE x MATRIX_SIZE`, split by row blocks across four
chiplets. The manager (`io`) sends each worker (`chiplet0..3`) its row block of
A plus all of B, each worker multiplies its rows and writes them back at their
offset in C, and the manager prints C as the blocks arrive. Both sides are driven
purely by the interrupt a landing write raises on the receiving core.

### Problem size

`MATRIX_SIZE` defaults to 4, one row of C per chiplet, and can be any multiple of
the chiplet count. Set it with `set_size.sh`, which updates `include/matmul.h` and
`workloads/matmul.cpp` together:

```sh
./set_size.sh 8
make run ARGS="--setup=matmul"
```

Both places need it: the cycle estimator decides whether a cached count is still
valid from the hash of the workload source alone, so a size taken only from the
shared header would leave a stale count behind.

The operands are generated from `element_a`/`element_b` rather than stored, so the
size costs nothing to change.

### Size ceiling

The manager sends each worker `5 * MATRIX_SIZE^2` bytes in a single AXI transfer,
and one transfer carries at most 256 beats of `axi.width` - 1024 bytes at the
32-bit default in `configs/chiplets/compute.yaml`. That caps `MATRIX_SIZE` at 12.
Widening `axi.width` in `system.yaml` raises it (64 bits reaches 20, 128 bits
reaches 28). Going further would mean splitting a block over several transfers,
which this setup deliberately does not do: one transfer per block each way is the
point of its control flow.

`set_size.sh` warns when a size crosses the cap, and `include/matmul.h` stops the
build with a `static_assert` naming the fix.
