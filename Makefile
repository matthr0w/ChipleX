BUILD_DIR := build
BUILD_DIR_DEBUG := build-debug
BUILD_DIR_ASAN := build-asan
SIM_BINARY := ./sim

# Per-tool Python environments
CE_VENV := tools/cycle_estimation/.venv
CE_REQ := tools/cycle_estimation/requirements.txt
CE_MAIN := tools/cycle_estimation/main.py

GUI_VENV := tools/gui/.venv
GUI_REQ := tools/gui/requirements.txt
GUI_MAIN := tools/gui/main.py

# Logging
LOG_INFO = printf "\033[0m[INFO]\033[0m  | %-16s | %s\n" "$(shell date +'%H:%M:%S')" 
LOG_WARN = printf \033[33m[WARN]\033[0m  | %-16s | %s\n" "$(shell date +'%H:%M:%S')" 
LOG_ERROR = printf "\033[31m[ERROR]\033[0m  | %-16s | %s\n" "$(shell date +'%H:%M:%S')" 

.PHONY: build release debug asan clean run gui test test-update

build: release

release:
	@$(LOG_INFO) "Building (Release)..."
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_SETUP= -DSETUPS_DIR=$(CURDIR)/setups
	@rm -f $(SIM_BINARY) setups/*/libsetup.so
	@cmake --build $(BUILD_DIR)
	@$(LOG_INFO) "Building (Release) done."

debug:
	@$(LOG_INFO) "Building (Debug)..."
	@mkdir -p $(BUILD_DIR_DEBUG)
	@cd $(BUILD_DIR_DEBUG) && cmake .. -DCMAKE_BUILD_TYPE=Debug
	@rm -f $(SIM_BINARY) setups/*/libsetup.so
	@cmake --build $(BUILD_DIR_DEBUG)
	@$(LOG_INFO) "Building (Debug) done."

asan:
	@$(LOG_INFO) "Building (ASan + UBSan)..."
	@mkdir -p $(BUILD_DIR_ASAN)
	@cd $(BUILD_DIR_ASAN) && cmake .. -DENABLE_ASAN=ON
	@rm -f $(SIM_BINARY) setups/*/libsetup.so
	@cmake --build $(BUILD_DIR_ASAN)
	@$(LOG_INFO) "Building (ASan + UBSan) done."

clean:
	@$(LOG_INFO) "Cleaning build directories and artifacts..."
	@rm -rf $(BUILD_DIR) $(BUILD_DIR_DEBUG) $(BUILD_DIR_ASAN)
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