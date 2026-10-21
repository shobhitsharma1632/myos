# Simple Makefile to bridge Toolchain and CMake Build

# Use absolute path based on the current directory
PROJECT_ROOT := $(shell pwd)
TOOLCHAIN_DIR := $(PROJECT_ROOT)/toolchain
BUILD_DIR := build
TOOLCHAIN_BUILD_DIR := $(PROJECT_ROOT)/toolchain_build

.PHONY: toolchain build run run-gui clean

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

run-gui:
	@if [ ! -f $(BUILD_DIR)/main_floppy.img ]; then \
		$(MAKE) build; \
	fi
	@if [ -n "$$DISPLAY" ] && [ "$(VNC)" != "1" ]; then \
		echo "Launching QEMU with native graphical display window..."; \
		qemu-system-i386 -drive file=$(BUILD_DIR)/main_floppy.img,format=raw,if=ide -smp 4 -audiodev id=snd0,driver=none -device sb16,audiodev=snd0 -netdev user,id=net0 -device e1000,netdev=net0 -debugcon stdio; \
	else \
		echo "================================================================="; \
		echo "  Headless environment detected (\$$DISPLAY is not set)."; \
		echo "  Starting QEMU with VNC server on port 5900 (display :0)..."; \
		echo "  WebSocket VNC enabled on port 6080."; \
		echo "  Connect your VNC client to localhost:5900 to interact with GUI!"; \
		echo "================================================================="; \
		qemu-system-i386 -drive file=$(BUILD_DIR)/main_floppy.img,format=raw,if=ide -smp 4 -audiodev id=snd0,driver=none -device sb16,audiodev=snd0 -netdev user,id=net0 -device e1000,netdev=net0 -vnc 0.0.0.0:0,websocket=6080 -debugcon stdio; \
	fi

clean:
	rm -rf $(BUILD_DIR)
