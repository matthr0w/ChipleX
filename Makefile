BUILD_DIR := build
BUILD_DIR_DEBUG := build-debug
BUILD_DIR_ASAN := build-asan
SIM_BINARY := ./sim

# Python
PYVENV := tools/.venv
PYREQ := tools/requirements.txt

# Tools
CYCLE_ESTIMATION := tools/cycle_estimation/main.py

# Logging
LOG_INFO = printf "\033[0m[INFO]\033[0m  | %-16s | %s\n" "$(shell date +'%H:%M:%S')" 
LOG_WARN = printf \033[33m[WARN]\033[0m  | %-16s | %s\n" "$(shell date +'%H:%M:%S')" 
LOG_ERROR = printf "\033[31m[ERROR]\033[0m  | %-16s | %s\n" "$(shell date +'%H:%M:%S')" 

.PHONY: build release debug asan clean run venv test test-update

# Default build is an optimized Release build.
build: release

release:
	@$(LOG_INFO) "Building (Release)..."
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake .. -DCMAKE_BUILD_TYPE=Release
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

venv:
	@if [ ! -d "$(PYVENV)" ]; then \
		$(LOG_INFO) "Creating Python virtual environment..."; \
		python3 -m venv $(PYVENV) > /dev/null; \
		$(PYVENV)/bin/pip install -r $(PYREQ) &> /dev/null; \
		$(LOG_INFO) "Creating Python virtual environment done."; \
	fi

run: build venv
	@$(PYVENV)/bin/python $(CYCLE_ESTIMATION)
	@SYSTEMC_DISABLE_COPYRIGHT_MESSAGE=1 $(SIM_BINARY) $(ARGS)
	@$(LOG_INFO) "Simulation finished."

# Regression: run every setup and diff stats.json against tests/golden/.
test: build
	@$(LOG_INFO) "Running regression harness..."
	@bash scripts/regression.sh

# Regenerate the golden stats after an intentional behavior change.
test-update: build
	@bash scripts/regression.sh --update