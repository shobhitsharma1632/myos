# FlippedBit OS

FlippedBit OS is a simple educational operating system project, written in C and Assembly, designed to demonstrate the basics of OS development, bootloader design, and FAT12 filesystem handling. This project includes a custom bootloader, a basic kernel, and userland tools for working with FAT images.

---

## Features

- **Bootloader**: Two-stage bootloader supporting FAT12 floppy images.
- **Kernel**: Simple kernel with basic memory management, I/O, and screen output.
- **FAT12 Support**: Read/write support for FAT12-formatted floppy images.
- **Cross-Compilation**: Automated toolchain setup for building i686-elf binaries.
- **Emulator Ready**: Easily bootable in emulators like Bochs or QEMU.
- **Tools**: Utilities for manipulating FAT images and testing the OS.

---

## Project Structure

```
myos/
├── Makefile
├── build_scripts/
│   ├── config.mk
│   └── toolchain.mk
├── src/
│   ├── bootloader/
│   │   ├── stage1/
│   │   └── stage2/
│   └── kernel/
├── tools/
│   └── fat/
└── test.txt
```

- **src/bootloader/stage1**: First-stage bootloader (Assembly)
- **src/bootloader/stage2**: Second-stage bootloader (C/Assembly)
- **src/kernel**: Main kernel code
- **tools/fat**: FAT12 manipulation utilities
- **build_scripts**: Toolchain and build configuration scripts

---

## Getting Started

### Prerequisites

- **Docker** (recommended for devcontainer) or Linux based development environment
- **GNU Make**
- **NASM** assembler
- **mtools** (for FAT image manipulation)
- **QEMU** (for emulation)
- **Bochs** (for debugging)

### Building the Toolchain

The project uses a cross-compiler toolchain. To build it:

```sh
cd <Project_dir_name>
make toolchain
```

This will download and build binutils and GCC for `i686-elf`.

### Building the OS

To build the OS and create a bootable floppy image:

```sh
make
```

This will produce `build/main_floppy.img`, a bootable FAT12 floppy image.

### Running in an Emulator

You can run the OS image using QEMU:

```sh
qemu-system-i386 -fda build/main_floppy.img
```

Or use the provided scripts:

```sh
./run.sh
# or
./debug.sh
```

### Cleaning Build Artifacts

```sh
make clean
```

---

## Development Environment

A [devcontainer](https://code.visualstudio.com/docs/remote/containers) is provided for easy setup in VS Code. Open the project in VS Code and reopen in the container when prompted.

---

## ToDo

- [ ] Replace make with cmake build system
- [ ] Explore options for a GUI
- [ ] Port the boot mode from legacy to UEFI.

## License

This project is for educational purposes. See individual files for license details.

---

## Credits

- Inspired by many OSDev tutorials and open-source projects.
- See source file headers for individual authorship.

---

Happy hacking! 🚀
