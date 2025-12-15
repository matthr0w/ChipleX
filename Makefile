BUILD_DIR := build
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

.PHONY: build clean run venv

build:
	@$(LOG_INFO) "Building simulation..."
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake .. && cmake --build .
	@$(LOG_INFO) "Building simulation done."

clean:
	@$(LOG_INFO) "Cleaning build directory..."
	@rm -rf $(BUILD_DIR)
	@$(LOG_INFO) "Cleaning build directory done."

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