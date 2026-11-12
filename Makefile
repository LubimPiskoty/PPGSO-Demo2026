BUILD_DIR := build
TARGET := semestralka
# nproc is Linux-only; fall back to the env var Windows sets, then to 4.
JOBS := $(shell nproc 2>/dev/null || echo $(NUMBER_OF_PROCESSORS))
JOBS := $(if $(JOBS),$(JOBS),4)
BUILD_TYPE ?=

.PHONY: run build debug release install clean distclean configure

run: build
	./$(BUILD_DIR)/$(TARGET)

configure:
	cmake -S . -B $(BUILD_DIR) $(if $(BUILD_TYPE),-DCMAKE_BUILD_TYPE=$(BUILD_TYPE))

build: configure
	cmake --build $(BUILD_DIR) -j$(JOBS)

debug:
	$(MAKE) build BUILD_TYPE=Debug

release:
	$(MAKE) build BUILD_TYPE=Release

# Stripped (and UPX-packed, if upx is found) executable into build/dist,
# or into PREFIX when given: make install PREFIX=~/somewhere
install: build
	cmake --install $(BUILD_DIR) $(if $(PREFIX),--prefix $(PREFIX))

clean:
	rm -rf $(BUILD_DIR)/CMakeFiles/$(TARGET).dir $(BUILD_DIR)/$(TARGET)

distclean:
	rm -rf $(BUILD_DIR)
