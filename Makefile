BUILD_DIR = build

build:
	mkdir -p $(BUILD_DIR) && cd $(BUILD_DIR) && cmake .. && make
	
clean:
	rm -rf $(BUILD_DIR)

.PHONY: build clean