BUILD_DIR := build
TARGET := semestralka
# nproc is Linux-only; fall back to the env var Windows sets, then to 4.
JOBS := $(shell nproc 2>/dev/null || echo $(NUMBER_OF_PROCESSORS))
JOBS := $(if $(JOBS),$(JOBS),4)
BUILD_TYPE ?=

.PHONY: run build release clean distclean configure

run: build
	./$(BUILD_DIR)/$(TARGET)

configure:
	cmake -S . -B $(BUILD_DIR) $(if $(BUILD_TYPE),-DCMAKE_BUILD_TYPE=$(BUILD_TYPE))

build: configure
	cmake --build $(BUILD_DIR) -j$(JOBS)

release:
	$(MAKE) build BUILD_TYPE=Release

clean:
	rm -rf $(BUILD_DIR)/CMakeFiles/$(TARGET).dir $(BUILD_DIR)/$(TARGET)

distclean:
	rm -rf $(BUILD_DIR)
