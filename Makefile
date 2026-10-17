# Simple Makefile to bridge Toolchain and CMake Build

# Use absolute path based on the current directory
PROJECT_ROOT := $(shell pwd)
TOOLCHAIN_DIR := $(PROJECT_ROOT)/toolchain
BUILD_DIR := build
TOOLCHAIN_BUILD_DIR := $(PROJECT_ROOT)/toolchain_build

.PHONY: toolchain build run clean

toolchain:
	mkdir -p $(TOOLCHAIN_DIR)
	bash build_scripts/setup_toolchain.sh "$(TOOLCHAIN_DIR)" "$(TOOLCHAIN_BUILD_DIR)"

build:
	rm -rf $(BUILD_DIR)
	cmake -B $(BUILD_DIR) \
		-DCMAKE_TOOLCHAIN_FILE=cmake/toolchain.cmake \
		-DCMAKE_C_COMPILER="$(TOOLCHAIN_DIR)/bin/i686-elf-gcc" \
		-DCMAKE_CXX_COMPILER="$(TOOLCHAIN_DIR)/bin/i686-elf-g++" \
		-DTOOLCHAIN_INSTALL_DIR="$(TOOLCHAIN_DIR)"
	cmake --build $(BUILD_DIR)

run:
	make -C $(BUILD_DIR) run

clean:
	rm -rf $(BUILD_DIR)
