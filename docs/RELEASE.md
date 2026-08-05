# Building a Release Bundle

The release is a single Linux executable (`chiplet-sim`) that bundles the
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
- `configs/`, `tools/cycle_estimation/`.

At runtime the bundle seeds an editable workspace under
`~/.local/share/chiplet-sim/` (setups and build outputs) so the read-only bundle
is never modified.

## Build locally

```bash
make bundle
```

The result is `dist/chiplet-sim`. SystemC is built into `.systemc-install/` on
first use; set `SYSTEMC_PATH` to reuse an existing install. Set `YAML_CPP_DIR` to
a prebuilt yaml-cpp (headers + `lib/libyaml-cpp.a`) instead of fetching it, for
offline builds.

## Build in CI

[.gitlab-ci.yml](../.gitlab-ci.yml) defines a `release:linux-x86_64` job that
runs `make bundle` (which builds and caches SystemC in `.systemc-install/`) and
publishes `dist/chiplet-sim` as a job artifact. It runs on tags and can be
started manually from the GitLab UI.