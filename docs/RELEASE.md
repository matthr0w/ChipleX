# Building a Release Bundle

The release is a single Linux executable (`chiplex`) that bundles the
GUI, a relocatable `sim`, the SystemC runtime, and the headers and dependencies
needed to compile setups on the target machine. It runs with no SystemC install
on the user's system; compiling new or edited setups additionally requires a C++
compiler and cmake on that machine.

## What the bundle contains

The build stages a `framework/` tree that PyInstaller embeds:

- `sim` - relocatable simulator (rpath `$ORIGIN`; finds SystemC in the bundle).
- `systemc/` - SystemC install (runtime library + headers for setup builds).
- `setups/` - the original setups, with their plugins pre-built so they run out
  of the box.
- `include/`, `CMakeLists.txt`, `deps/yaml-cpp/` - everything needed to compile a
  setup plugin offline on the target.
- `configs/` - the default module parameters.
- `tools/cycle-estimation` - the cycle estimator frozen as a standalone
  executable (its own PyInstaller build, with PyYAML and the gem5 model data
  embedded) so estimation runs without a system Python.
- `tools/cycle_estimation/gem5/` - the CPU-model manifests and reference config
  script, read by the setup editor to populate the per-chiplet gem5 block.

At runtime the bundle seeds an editable workspace under
`~/.local/share/chiplex/` (setups, build outputs, and a `gem5-models/`
directory for user-added CPU models) so the read-only bundle is never modified.

## Build locally

```bash
make bundle
```

The result is `dist/chiplex`. SystemC is built into `.systemc-install/` on
first use; set `SYSTEMC_PATH` to reuse an existing install. Set `YAML_CPP_DIR` to
a prebuilt yaml-cpp (headers + `lib/libyaml-cpp.a`) instead of fetching it, for
offline builds.

## Build in CI

[.github/workflows/release.yml](../.github/workflows/release.yml) runs
`make bundle` (which builds and caches SystemC in `.systemc-install/`) inside
an `ubuntu:22.04` container, so the bundle links against glibc 2.35 and stays
runnable on Ubuntu 22.04 and later. It is split into two jobs:

- `build-linux-x86_64` builds the bundle and uploads `dist/chiplex` as a
  workflow artifact.
- `publish-release` runs only for tags. It creates the GitHub Release for the
  tag with generated notes and attaches the bundle as
  `chiplex-linux-x86_64.tar.gz`.

Cutting a release is therefore a tag push:

```bash
git tag v1.0.0
git push origin v1.0.0
```

`workflow_dispatch` builds the bundle
without creating a release. Add `--draft` to the `gh release create` call if
releases should be reviewed before going public.