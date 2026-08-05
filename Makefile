BUILD_DIR := build
BUILD_DIR_RELEASE := build-release
BUILD_DIR_DEBUG := build-debug
BUILD_DIR_ASAN := build-asan
SIM_BINARY := ./sim

# SystemC: build a local install by default (into .systemc-install); set
# SYSTEMC_PATH to use an existing install instead. Builds depend on `systemc`,
# and the runtime library path is exported so the built sim runs directly.
SYSTEMC_VERSION ?= 3.0.1
SYSTEMC_PREFIX ?= $(CURDIR)/.systemc-install
SYSTEMC_PATH ?= $(SYSTEMC_PREFIX)
export SYSTEMC_PATH
export LD_LIBRARY_PATH := $(SYSTEMC_PATH)/lib:$(SYSTEMC_PATH)/lib64$(if $(LD_LIBRARY_PATH),:$(LD_LIBRARY_PATH))

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
GUI_SPEC := tools/gui/chiplet-sim.spec

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
	@$(CE_VENV)/bin/python $(CE_MAIN)
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
	@if [ -f "$(SYSTEMC_PATH)/lib/libsystemc.so" ] || [ -f "$(SYSTEMC_PATH)/lib64/libsystemc.so" ]; then \
		:; \
	elif [ "$(SYSTEMC_PATH)" != "$(SYSTEMC_PREFIX)" ]; then \
		$(LOG_ERROR) "No libsystemc.so under SYSTEMC_PATH=$(SYSTEMC_PATH). Point it at a SystemC install or unset it to build a local one."; \
		exit 1; \
	else \
		$(LOG_INFO) "Building SystemC $(SYSTEMC_VERSION) (one-time, into $(SYSTEMC_PREFIX))..."; \
		tmp=$$(mktemp -d); \
		git clone --depth 1 --branch $(SYSTEMC_VERSION) https://github.com/accellera-official/systemc.git $$tmp/systemc; \
		cmake -S $$tmp/systemc -B $$tmp/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=17 -DCMAKE_INSTALL_PREFIX="$(SYSTEMC_PREFIX)"; \
		cmake --build $$tmp/build -j$$(nproc); \
		cmake --install $$tmp/build; \
		rm -rf $$tmp; \
		$(LOG_INFO) "SystemC $(SYSTEMC_VERSION) installed to $(SYSTEMC_PREFIX)"; \
	fi

# Build the self-contained GUI release bundle (dist/chiplet-sim).
# Requires SYSTEMC_PATH; set YAML_CPP_DIR to build yaml-cpp offline.
define BUNDLE_RECIPE
set -euo pipefail
: "$${SYSTEMC_PATH:?Set SYSTEMC_PATH to a SystemC install (include/ and lib/libsystemc.so)}"
rm -rf "$(CURDIR)/$(STAGE_DIR)"
mkdir -p "$(CURDIR)/$(STAGE_DIR)"
cp -r "$(CURDIR)/setups" "$(CURDIR)/$(STAGE_DIR)/setups"
if [ -n "$${YAML_CPP_DIR:-}" ]; then YCPP="-DYAML_CPP_DIR=$$YAML_CPP_DIR"; else YCPP="-DYAML_CPP_DIR="; fi
cmake -S "$(CURDIR)" -B "$(CURDIR)/$(BUILD_DIR_RELEASE)" -DCMAKE_BUILD_TYPE=Release -DRELOCATABLE=ON -DSETUPS_DIR="$(CURDIR)/$(STAGE_DIR)/setups" "$$YCPP"
cmake --build "$(CURDIR)/$(BUILD_DIR_RELEASE)" -j"$$(nproc)"
cp "$(CURDIR)/$(BUILD_DIR_RELEASE)/sim" "$(CURDIR)/$(STAGE_DIR)/sim"
chmod +x "$(CURDIR)/$(STAGE_DIR)/sim"
mkdir -p "$(CURDIR)/$(STAGE_DIR)/systemc"
cp -r "$$SYSTEMC_PATH/include" "$(CURDIR)/$(STAGE_DIR)/systemc/include"
for d in lib lib64; do if [ -d "$$SYSTEMC_PATH/$$d" ]; then cp -rP "$$SYSTEMC_PATH/$$d" "$(CURDIR)/$(STAGE_DIR)/systemc/$$d"; fi; done
while IFS= read -r link; do tgt="$$(readlink -f "$$link")"; rm -f "$$link"; cp "$$tgt" "$$link"; done < <(find "$(CURDIR)/$(STAGE_DIR)/systemc" -type l)
cp -r "$(CURDIR)/include" "$(CURDIR)/$(STAGE_DIR)/include"
cp "$(CURDIR)/CMakeLists.txt" "$(CURDIR)/$(STAGE_DIR)/CMakeLists.txt"
cp -r "$(CURDIR)/configs" "$(CURDIR)/$(STAGE_DIR)/configs"
mkdir -p "$(CURDIR)/$(STAGE_DIR)/deps/yaml-cpp/lib" "$(CURDIR)/$(STAGE_DIR)/deps/yaml-cpp/include"
if [ -n "$${YAML_CPP_DIR:-}" ]; then YI="$$YAML_CPP_DIR/include/yaml-cpp"; YL="$$YAML_CPP_DIR/lib/libyaml-cpp.a"; else YI="$(CURDIR)/$(BUILD_DIR_RELEASE)/_deps/yaml-cpp-src/include/yaml-cpp"; YL="$(CURDIR)/$(BUILD_DIR_RELEASE)/_deps/yaml-cpp-build/libyaml-cpp.a"; fi
if [ ! -d "$$YI" ] || [ ! -f "$$YL" ]; then echo "yaml-cpp artifacts not found: $$YI, $$YL" >&2; exit 1; fi
cp -r "$$YI" "$(CURDIR)/$(STAGE_DIR)/deps/yaml-cpp/include/"
cp "$$YL" "$(CURDIR)/$(STAGE_DIR)/deps/yaml-cpp/lib/"
mkdir -p "$(CURDIR)/$(STAGE_DIR)/tools"
cp -r "$(CURDIR)/tools/cycle_estimation" "$(CURDIR)/$(STAGE_DIR)/tools/cycle_estimation"
if [ ! -d "$(RELEASE_VENV)" ]; then python3 -m venv "$(RELEASE_VENV)"; fi
"$(RELEASE_VENV)/bin/pip" install -q --upgrade pip
"$(RELEASE_VENV)/bin/pip" install -q -r "$(GUI_REQ)" pyinstaller
REPO_ROOT="$(CURDIR)" RELEASE_STAGE="$(CURDIR)/$(STAGE_DIR)" "$(RELEASE_VENV)/bin/pyinstaller" --clean --noconfirm --distpath "$(CURDIR)/$(DIST_DIR)" --workpath "$(CURDIR)/$(BUILD_DIR_RELEASE)/pyinstaller" "$(CURDIR)/$(GUI_SPEC)"
endef
export BUNDLE_RECIPE

bundle: systemc
	@$(LOG_INFO) "Building release bundle..."
	@bash -c "$$BUNDLE_RECIPE"
	@$(LOG_INFO) "Release bundle ready: $(DIST_DIR)/chiplet-sim"