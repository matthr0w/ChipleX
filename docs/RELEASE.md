# Building a Release Bundle

The release is a single executable (`chiplex`) that bundles the GUI, a
relocatable `sim`, the SystemC runtime, and the headers and dependencies needed
to compile setups on the target machine. It runs with no SystemC install on the
user's system; compiling new or edited setups additionally requires a C++
compiler and cmake on that machine.

One bundle is published per target: Linux x86_64 and macOS on Apple Silicon.

## What the bundle contains

The build stages a `framework/` tree that PyInstaller embeds:

- `sim` - relocatable simulator (rpath `$ORIGIN` on Linux, `@loader_path` on
  macOS; finds SystemC in the bundle).
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
`make bundle` (which builds and caches SystemC in `.systemc-install/`) once per
target. It is split into three jobs:

- `build-linux-x86_64` builds inside an `ubuntu:22.04` container, so the bundle
  links against glibc 2.35 and stays runnable on Ubuntu 22.04 and later.
- `build-macos-arm64` builds on the pinned `macos-14` runner. It additionally
  verifies that no staged Mach-O still references the build workspace and that
  every binary is arm64, because a bundle whose install names were not rewritten
  would run on the runner and nowhere else.
- `publish-release` runs only for tags. It collects every `chiplex-*` artifact,
  archives each as `<artifact-name>.tar.gz`, creates the GitHub Release for the
  tag with generated notes, and attaches them all.

Cutting a release is therefore a tag push:

```bash
git tag v1.0.0
git push origin v1.0.0
```

`workflow_dispatch` builds the bundle
without creating a release. Add `--draft` to the `gh release create` call if
releases should be reviewed before going public.
## Spawning external programs from the bundle

The PyInstaller bootloader puts the bundle's extraction directory on the
loader's search path so the frozen GUI finds its own libraries. That directory
holds the C++ runtime of the machine the bundle was built on. Any child process
that is a system binary on the target - `cmake`, the compiler, gem5 - must
therefore not inherit it, or it loads that older runtime in preference to its
own and fails to resolve symbols it was linked against.

Every spawn site goes through `base_child_env()` in
[tools/gui/app/project.py](../tools/gui/app/project.py), which restores the
caller's original search path (or removes the variable when there was none)
before the child is launched. Building the environment with `dict(os.environ)`
directly reintroduces the fault, which only shows up on a target whose
toolchain is newer than the build container's.

## macOS specifics

Three differences from the Linux bundle are worth knowing when changing the
build:

- **Plugin symbol resolution.** Setup plugins call into `sim` itself, so they
  carry undefined symbols that only the executable defines. Linux resolves this
  with `-rdynamic` plus lazy binding. ld64's two-level namespace rejects it at
  link time, so plugins are linked with `-undefined dynamic_lookup` and `sim`
  with `-export_dynamic`; the symbols are then bound by a flat-namespace lookup
  at `dlopen` time.
- **Install names.** dyld resolves a dependency through the install name baked
  into the dylib, not through a search path, so `make bundle` rewrites every
  SystemC reference in `sim` and in the prebuilt plugins to `@rpath/...` and
  re-signs each binary afterwards. `install_name_tool` invalidates a signature,
  and arm64 refuses to execute an unsigned Mach-O.
- **Gatekeeper.** The bundle is ad-hoc signed, which is enough to execute
  locally but not to satisfy Gatekeeper. Users who download the archive through
  a browser must clear the quarantine flag once
  (`xattr -dr com.apple.quarantine ./chiplex`). Removing that step means
  notarizing the bundle, which requires a paid Apple Developer account and
  signing credentials stored as repository secrets.

The plugin filename stays `libsetup.so` on macOS rather than becoming
`libsetup.dylib`: `dlopen` resolves it by path, and keeping one name means the
loader and the GUI need no platform-specific lookup.
