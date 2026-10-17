#!/bin/bash

# QEMU LBA Support Diagnostic Test Script
# This script builds and runs a diagnostic bootloader to test BIOS int13h support

set -e

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$PROJECT_DIR/build_diagnostic"
DIAGNOSTIC_ASM="$PROJECT_DIR/diagnostic_bootloader.asm"

echo "=========================================="
echo "QEMU LBA Support Diagnostic"
echo "=========================================="
echo

# Check if diagnostic bootloader exists
if [ ! -f "$DIAGNOSTIC_ASM" ]; then
    echo "Error: $DIAGNOSTIC_ASM not found"
    exit 1
fi

# Create build directory
mkdir -p "$BUILD_DIR"

# Assemble bootloader
echo "[1/3] Assembling diagnostic bootloader..."
nasm -f bin "$DIAGNOSTIC_ASM" -o "$BUILD_DIR/diagnostic.bin"
echo "      ✓ Created diagnostic.bin ($(stat -f%z "$BUILD_DIR/diagnostic.bin" 2>/dev/null || stat -c%s "$BUILD_DIR/diagnostic.bin") bytes)"

# Create floppy image
echo "[2/3] Creating floppy image..."
dd if=/dev/zero of="$BUILD_DIR/diagnostic.img" bs=512 count=2880 2>/dev/null
dd if="$BUILD_DIR/diagnostic.bin" of="$BUILD_DIR/diagnostic.img" bs=512 count=1 conv=notrunc 2>/dev/null
echo "      ✓ Created diagnostic.img (1.44MB floppy)"

# Run in QEMU
echo "[3/3] Running diagnostic in QEMU..."
echo "      (output will appear below)"
echo
echo "=========================================="

qemu-system-i386 \
    -drive file="$BUILD_DIR/diagnostic.img",format=raw,if=floppy \
    -nographic \
    -serial mon:stdio \
    -no-shutdown \
    -no-reboot \
    2>&1 | head -100

echo
echo "=========================================="
echo
echo "To interpret the results:"
echo
echo "  ✓ LBA SUPPORTED        = QEMU BIOS supports int13h AH=41h (check extensions)"
echo "  ✗ LBA NOT SUPPORTED    = QEMU BIOS does NOT support LBA (this is expected)"
echo "  ✓ CHS SUPPORTED        = QEMU BIOS supports int13h AH=08h (get drive params)"
echo "  ✓ CHS READ SUCCESSFUL  = QEMU BIOS can read sectors (AH=02h works)"
echo
echo "Expected QEMU output: CHS works, LBA doesn't (confirming QEMU limitation)"
echo
