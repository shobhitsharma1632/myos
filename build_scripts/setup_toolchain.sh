#!/bin/bash
# scripts/setup_toolchain.sh
# Adapted from nanobyte_os to be invoked by CMake

set -e

TARGET="i686-elf"
BINUTILS_VERSION="2.37"
GCC_VERSION="11.2.0"
TOOLCHAIN_DIR="$1"
BUILD_DIR="$2"

if [ -z "$TOOLCHAIN_DIR" ] || [ -z "$BUILD_DIR" ]; then
    echo "Usage: $0 <toolchain_dir> <build_dir>"
    exit 1
fi

# Only proceed if the compiler does not already exist
if [ -f "${TOOLCHAIN_DIR}/bin/${TARGET}-gcc" ]; then
    echo "Toolchain already installed in ${TOOLCHAIN_DIR}. Skipping build."
    exit 0
fi

mkdir -p "$TOOLCHAIN_DIR"
mkdir -p "$BUILD_DIR"

# 1. Build Binutils
BINUTILS_SRC="binutils-${BINUTILS_VERSION}"
BINUTILS_BUILD="${BUILD_DIR}/binutils-build"

pushd "$BUILD_DIR"
if [ ! -d "$BINUTILS_SRC" ]; then
    wget -nc "https://ftp.gnu.org/gnu/binutils/binutils-${BINUTILS_VERSION}.tar.xz"
    tar -xf "binutils-${BINUTILS_VERSION}.tar.xz"
fi

mkdir -p "$BINUTILS_BUILD"
pushd "$BINUTILS_BUILD"
"${BUILD_DIR}/${BINUTILS_SRC}"/configure \
    --prefix="$TOOLCHAIN_DIR" \
    --target="$TARGET" \
    --disable-nls \
    --disable-werror
make -j$(nproc)
make install
popd
popd

# 2. Build GCC
GCC_SRC="gcc-${GCC_VERSION}"
GCC_BUILD="${BUILD_DIR}/gcc-build"

pushd "$BUILD_DIR"
if [ ! -d "$GCC_SRC" ]; then
    wget -nc "https://ftp.gnu.org/gnu/gcc/gcc-${GCC_VERSION}/gcc-${GCC_VERSION}.tar.xz"
    tar -xf "gcc-${GCC_VERSION}.tar.xz"
fi

mkdir -p "$GCC_BUILD"
pushd "$GCC_BUILD"

"${BUILD_DIR}/${GCC_SRC}"/configure \
    --prefix="$TOOLCHAIN_DIR" \
    --target="$TARGET" \
    --disable-nls \
    --enable-languages=c,c++ \
    --without-headers
make -j$(nproc) all-gcc all-target-libgcc
make install-gcc install-target-libgcc
popd
popd
