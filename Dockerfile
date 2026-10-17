# Stage 1: Build the Toolchain
FROM ubuntu:22.04 AS builder

# Install build dependencies
RUN apt-get update && apt-get install -y \
    wget \
    tar \
    build-essential \
    flex \
    bison \
    libgmp-dev \
    libmpfr-dev \
    libmpc-dev \
    texinfo \
    cmake \
    nasm \
    dosfstools \
    mtools \
    && rm -rf /var/lib/apt/lists/*

# Set up toolchain variables (using same versions as nanobyte_os)
ENV TARGET=i686-elf
ENV TOOLCHAIN_PREFIX=/usr/local/cross
ENV PATH="${TOOLCHAIN_PREFIX}/bin:${PATH}"
ENV BINUTILS_VERSION=2.37
ENV GCC_VERSION=11.2.0

WORKDIR /build

# Build Binutils
RUN wget https://ftp.gnu.org/gnu/binutils/binutils-${BINUTILS_VERSION}.tar.xz && \
    tar -xf binutils-${BINUTILS_VERSION}.tar.xz && \
    mkdir binutils-build && \
    cd binutils-build && \
    ../binutils-${BINUTILS_VERSION}/configure \
        --prefix="${TOOLCHAIN_PREFIX}" \
        --target=${TARGET} \
        --disable-nls \
        --disable-werror && \
    make -j$(nproc) && \
    make install

# Build GCC
RUN wget https://ftp.gnu.org/gnu/gcc/gcc-${GCC_VERSION}/gcc-${GCC_VERSION}.tar.xz && \
    tar -xf gcc-${GCC_VERSION}.tar.xz && \
    mkdir gcc-build && \
    cd gcc-build && \
    ../gcc-${GCC_VERSION}/configure \
        --prefix="${TOOLCHAIN_PREFIX}" \
        --target=${TARGET} \
        --disable-nls \
        --enable-languages=c,c++ \
        --without-headers && \
    make -j$(nproc) all-gcc all-target-libgcc && \
    make install-gcc install-target-libgcc

# Stage 2: Final Build Environment
FROM ubuntu:22.04

# Install build tools needed for the OS build itself
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    nasm \
    dosfstools \
    mtools \
    && rm -rf /var/lib/apt/lists/*

# Copy the built cross-compiler from builder stage
COPY --from=builder /usr/local/cross /usr/local/cross
ENV PATH="/usr/local/cross/bin:${PATH}"

WORKDIR /workspaces/myos

# Default command
CMD ["/bin/bash"]
