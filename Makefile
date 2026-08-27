BUILD_DIR := build
BUILD_DIR_RELEASE := build-release
BUILD_DIR_DEBUG := build-debug
BUILD_DIR_ASAN := build-asan
SIM_BINARY := ./sim

# Host platform. Linux and macOS differ in the shared-library suffix, the
# loader's search-path variable, and the parallelism query, so resolve all
# three once here rather than at each use site.
UNAME_S := $(shell uname -s)
NPROC := $(shell getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)

# SystemC: build a local install by default (into .systemc-install); set
# SYSTEMC_PATH to use an existing install instead. Builds depend on `systemc`,
# and the runtime library path is exported so the built sim runs directly.
SYSTEMC_VERSION ?= 3.0.1
SYSTEMC_PREFIX ?= $(CURDIR)/.systemc-install
SYSTEMC_PATH ?= $(SYSTEMC_PREFIX)
export SYSTEMC_PATH

ifeq ($(UNAME_S),Darwin)
  SHLIB_EXT := dylib
  export DYLD_LIBRARY_PATH := $(SYSTEMC_PATH)/lib:$(SYSTEMC_PATH)/lib64$(if $(DYLD_LIBRARY_PATH),:$(DYLD_LIBRARY_PATH))
  # Pin the ABI floor so the artifact does not silently inherit the build
  # machine's OS version as its minimum. macOS 11 is the earliest release that
  # runs on Apple Silicon, so it costs nothing. As a side effect it keeps ld64
  # on classic binding rather than the chained fixups used from macOS 12,
  # which avoids the linker's dynamic_lookup-with-chained-fixups warning on the
  # setup plugins. Exported so the SystemC, yaml-cpp, simulator and PyInstaller
  # builds all agree on one value.
  export MACOSX_DEPLOYMENT_TARGET ?= 11.0
else
  SHLIB_EXT := so
  export LD_LIBRARY_PATH := $(SYSTEMC_PATH)/lib:$(SYSTEMC_PATH)/lib64$(if $(LD_LIBRARY_PATH),:$(LD_LIBRARY_PATH))
endif

# Per-tool Python environments
CE_VENV := tools/cycle_estimation/.venv
CE_REQ := tools/cycle_estimation/requirements.txt
CE_MAIN := tools/cycle_estimation/main.py
GUI_VENV := tools/gui/.venv
GUI_REQ := tools/gui/requirements.txt
GUI_MAIN := tools/gui/main.py

# Release bundle
DIST_DIR := dist
STAGE_DIR := $(DIST_DIR)/framework
RELEASE_VENV := $(BUILD_DIR_RELEASE)/venv
GUI_SPEC := tools/gui/chiplex.spec
CE_SPEC := tools/cycle_estimation/cycle-estimation.spec

# Logging
LOG_INFO = printf "\033[0m[INFO]\033[0m  | %-16s | %s\n" "$(shell date +'%H:%M:%S')" 
LOG_WARN = printf \033[33m[WARN]\033[0m  | %-16s | %s\n" "$(shell date +'%H:%M:%S')" 
LOG_ERROR = printf "\033[31m[ERROR]\033[0m  | %-16s | %s\n" "$(shell date +'%H:%M:%S')" 

.PHONY: build release debug asan clean run gui test test-update systemc bundle

build: release

release: systemc
	@$(LOG_INFO) "Building (Release)..."
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_SETUP= -DSETUPS_DIR=$(CURDIR)/setups
	@rm -f $(SIM_BINARY) setups/*/libsetup.so
	@cmake --build $(BUILD_DIR)
	@$(LOG_INFO) "Building (Release) done."

debug: systemc
	@$(LOG_INFO) "Building (Debug)..."
	@mkdir -p $(BUILD_DIR_DEBUG)
	@cd $(BUILD_DIR_DEBUG) && cmake .. -DCMAKE_BUILD_TYPE=Debug
	@rm -f $(SIM_BINARY) setups/*/libsetup.so
	@cmake --build $(BUILD_DIR_DEBUG)
	@$(LOG_INFO) "Building (Debug) done."

asan: systemc
	@$(LOG_INFO) "Building (ASan + UBSan)..."
	@mkdir -p $(BUILD_DIR_ASAN)
	@cd $(BUILD_DIR_ASAN) && cmake .. -DENABLE_ASAN=ON
	@rm -f $(SIM_BINARY) setups/*/libsetup.so
	@cmake --build $(BUILD_DIR_ASAN)
	@$(LOG_INFO) "Building (ASan + UBSan) done."

clean:
	@$(LOG_INFO) "Cleaning build directories and artifacts..."
	@rm -rf $(BUILD_DIR) $(BUILD_DIR_DEBUG) $(BUILD_DIR_ASAN) $(BUILD_DIR_RELEASE) $(DIST_DIR)
	@rm -f $(SIM_BINARY) setups/*/libsetup.so
	@$(LOG_INFO) "Cleaning done."

run: build
	@if [ ! -d "$(CE_VENV)" ]; then \
		$(LOG_INFO) "Creating virtual environment..."; \
		python3 -m venv $(CE_VENV) > /dev/null; \
		$(CE_VENV)/bin/pip install -r $(CE_REQ) > /dev/null; \
		$(LOG_INFO) "Creating virtual environment done."; \
	fi
	@$(CE_VENV)/bin/python $(CE_MAIN) || [ $$? -eq 2 ]
	@SYSTEMC_DISABLE_COPYRIGHT_MESSAGE=1 $(SIM_BINARY) $(ARGS)
	@$(LOG_INFO) "Simulation finished."

gui: build
	@if [ ! -d "$(GUI_VENV)" ]; then \
		$(LOG_INFO) "Creating virtual environment..."; \
		python3 -m venv $(GUI_VENV) > /dev/null; \
		$(GUI_VENV)/bin/pip install -r $(GUI_REQ) > /dev/null; \
		$(LOG_INFO) "Creating virtual environment done."; \
	fi
	@$(GUI_VENV)/bin/python $(GUI_MAIN)

# Regression: run every setup and diff stats.json against tests/golden/.
test: build
	@$(LOG_INFO) "Running regression harness..."
	@bash scripts/regression.sh

# Regenerate the golden stats after an intentional behavior change.
test-update: build
	@bash scripts/regression.sh --update

# Ensure SystemC is available at SYSTEMC_PATH. When SYSTEMC_PATH is the default
# managed install, build it on first use; when the user points it elsewhere,
# require it to already exist.
systemc:
	@if [ -f "$(SYSTEMC_PATH)/lib/libsystemc.$(SHLIB_EXT)" ] || [ -f "$(SYSTEMC_PATH)/lib64/libsystemc.$(SHLIB_EXT)" ]; then \
		:; \
	elif [ "$(SYSTEMC_PATH)" != "$(SYSTEMC_PREFIX)" ]; then \
		$(LOG_ERROR) "No libsystemc.$(SHLIB_EXT) under SYSTEMC_PATH=$(SYSTEMC_PATH). Point it at a SystemC install or unset it to build a local one."; \
		exit 1; \
	else \
		$(LOG_INFO) "Building SystemC $(SYSTEMC_VERSION) (one-time, into $(SYSTEMC_PREFIX))..."; \
		tmp=$$(mktemp -d); \
		git clone --depth 1 --branch $(SYSTEMC_VERSION) https://github.com/accellera-official/systemc.git $$tmp/systemc; \
		cmake -S $$tmp/systemc -B $$tmp/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=17 -DCMAKE_INSTALL_PREFIX="$(SYSTEMC_PREFIX)"; \
		cmake --build $$tmp/build -j$(NPROC); \
		cmake --install $$tmp/build; \
		rm -rf $$tmp; \
		$(LOG_INFO) "SystemC $(SYSTEMC_VERSION) installed to $(SYSTEMC_PREFIX)"; \
	fi

# Build the self-contained GUI release bundle (dist/chiplex).
# Requires SYSTEMC_PATH; set YAML_CPP_DIR to build yaml-cpp offline.
define BUNDLE_RECIPE
set -euo pipefail
: "$${SYSTEMC_PATH:?Set SYSTEMC_PATH to a SystemC install (include/ and lib/libsystemc.$(SHLIB_EXT))}"

ROOT="$(CURDIR)"
STAGE="$$ROOT/$(STAGE_DIR)"
BUILD="$$ROOT/$(BUILD_DIR_RELEASE)"
VENV="$$ROOT/$(RELEASE_VENV)"

# Start from an empty staging tree.
rm -rf "$$STAGE"
mkdir -p "$$STAGE"

# Stage the tracked sources needed to compile setup plugins offline, then
# build the relocatable simulator. SETUPS_DIR points at the staged setups so
# their plugins are compiled in place and ship pre-built.
cp -r "$$ROOT/setups" "$$STAGE/setups"
cp -r "$$ROOT/include" "$$STAGE/include"
cp -r "$$ROOT/configs" "$$STAGE/configs"
cp "$$ROOT/CMakeLists.txt" "$$STAGE/CMakeLists.txt"
if [ -n "$${YAML_CPP_DIR:-}" ]; then YCPP="-DYAML_CPP_DIR=$$YAML_CPP_DIR"; else YCPP="-DYAML_CPP_DIR="; fi
cmake -S "$$ROOT" -B "$$BUILD" -DCMAKE_BUILD_TYPE=Release -DRELOCATABLE=ON -DSETUPS_DIR="$$STAGE/setups" "$$YCPP"
cmake --build "$$BUILD" -j"$(NPROC)"
install -m 0755 "$$BUILD/sim" "$$STAGE/sim"

# Stage the SystemC install, dereferencing the versioned library symlinks so
# the bundle carries real files rather than dangling links.
mkdir -p "$$STAGE/systemc"
cp -r "$$SYSTEMC_PATH/include" "$$STAGE/systemc/include"
for d in lib lib64; do if [ -d "$$SYSTEMC_PATH/$$d" ]; then cp -rP "$$SYSTEMC_PATH/$$d" "$$STAGE/systemc/$$d"; fi; done
while IFS= read -r link; do tgt="$$(readlink -f "$$link")"; rm -f "$$link"; cp "$$tgt" "$$link"; done < <(find "$$STAGE/systemc" -type l)

# macOS resolves each dependency through the install name baked into the
# dylib rather than through a search path, so a bundle staged here would
# still name the build machine's SystemC prefix. Rewrite every SystemC
# reference to the @rpath form that the RELOCATABLE rpath resolves. sim and
# the prebuilt plugins must be rewritten alike: dyld considers two images the
# same library only when their install name strings match, so a plugin left
# naming an absolute path would pull in a second copy of the SystemC runtime
# instead of sharing the one sim already loaded.
if [ "$(UNAME_S)" = "Darwin" ]; then
  for lib in "$$STAGE"/systemc/lib*/libsystemc*.dylib; do
    [ -f "$$lib" ] || continue
    install_name_tool -id "@rpath/$$(basename "$$lib")" "$$lib"
    codesign --force --sign - "$$lib"
  done
  for macho in "$$STAGE/sim" "$$STAGE"/setups/*/libsetup.so; do
    [ -f "$$macho" ] || continue
    deps=$$(otool -L "$$macho" | awk 'NR > 1 { print $$1 }' | grep libsystemc || true)
    for dep in $$deps; do
      install_name_tool -change "$$dep" "@rpath/$$(basename "$$dep")" "$$macho"
    done
    # install_name_tool invalidates the code signature, and arm64 refuses to
    # execute an unsigned Mach-O at all, so re-apply an ad-hoc signature.
    codesign --force --sign - "$$macho"
  done
fi

# Stage vendored yaml-cpp (an override tree, else the CMake fetch output).
mkdir -p "$$STAGE/deps/yaml-cpp/lib" "$$STAGE/deps/yaml-cpp/include"
if [ -n "$${YAML_CPP_DIR:-}" ]; then YI="$$YAML_CPP_DIR/include/yaml-cpp"; YL="$$YAML_CPP_DIR/lib/libyaml-cpp.a"; else YI="$$BUILD/_deps/yaml-cpp-src/include/yaml-cpp"; YL="$$BUILD/_deps/yaml-cpp-build/libyaml-cpp.a"; fi
if [ ! -d "$$YI" ] || [ ! -f "$$YL" ]; then echo "yaml-cpp artifacts not found: $$YI, $$YL" >&2; exit 1; fi
cp -r "$$YI" "$$STAGE/deps/yaml-cpp/include/"
cp "$$YL" "$$STAGE/deps/yaml-cpp/lib/"

# Provision the Python toolchain shared by both PyInstaller builds.
if [ ! -d "$$VENV" ]; then python3 -m venv "$$VENV"; fi
"$$VENV/bin/pip" install -q --upgrade pip
"$$VENV/bin/pip" install -q -r "$$ROOT/$(GUI_REQ)" pyinstaller

# Freeze the cycle estimator to its own executable and stage the CPU-model
# data the setup editor reads. Both must exist before the GUI build below,
# which embeds the staging tree.
mkdir -p "$$STAGE/tools/cycle_estimation"
cp -r "$$ROOT/tools/cycle_estimation/gem5" "$$STAGE/tools/cycle_estimation/gem5"
CE_TOOL_DIR="$$ROOT/tools/cycle_estimation" "$$VENV/bin/pyinstaller" --clean --noconfirm --distpath "$$BUILD/ce-dist" --workpath "$$BUILD/ce-pyinstaller" "$$ROOT/$(CE_SPEC)"
install -m 0755 "$$BUILD/ce-dist/cycle-estimation" "$$STAGE/tools/cycle-estimation"

# Freeze the GUI, embedding the staging tree as its framework/ payload.
REPO_ROOT="$$ROOT" RELEASE_STAGE="$$STAGE" "$$VENV/bin/pyinstaller" --clean --noconfirm --distpath "$$ROOT/$(DIST_DIR)" --workpath "$$BUILD/pyinstaller" "$$ROOT/$(GUI_SPEC)"
endef
export BUNDLE_RECIPE

bundle: systemc
	@$(LOG_INFO) "Building release bundle..."
	@bash -c "$$BUNDLE_RECIPE"
	@$(LOG_INFO) "Release bundle ready: $(DIST_DIR)/chiplex"