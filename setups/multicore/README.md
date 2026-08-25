## Tile Blur

This setup blurs `data/input.png` band by band on a compute chiplet, staging the
frame in a memory chiplet and collecting the result back. With one core it is the
sequential baseline; with several cores the bands are claimed from a shared work
queue and filtered in parallel.

Use `set_cores.sh` to switch between the two, keeping `cores.num` (system.yaml),
`NUM_CORES` (src/program.cpp) and the per-core cycle estimates in sync:

```sh
./set_cores.sh 1   # sequential baseline
./set_cores.sh 4   # four cores in parallel
```
