# myOS — 32-Bit x86 Protected Mode Operating System

**myOS** is an educational, modular 32-bit x86 operating system built from the ground up in C and Assembly. It features a complete two-stage bootloader with MBR partition and FAT filesystem support, an ELF32 kernel with C runtime initialization, a modern Hardware Abstraction Layer (HAL), and an enterprise-grade memory management stack (Physical Memory Manager, Virtual Memory Manager with Paging, and a Kernel Heap Allocator).

---

## Architecture Overview

```
                      ┌────────────────────────────────────────┐
                      │    Kernel Core & Application Space     │
                      └───────────────────┬────────────────────┘
                                          │
                      ┌───────────────────▼────────────────────┐
                      │  MLFQ Preemptive Task Scheduler (HAL)  │
                      │  3-Level Queues, Stack Switching, ISR  │
                      └───────────────────┬────────────────────┘
                                          │
                      ┌───────────────────▼────────────────────┐
                      │    Kernel Heap (kmalloc / kfree)       │
                      │  Boundary-tag allocator, 8-byte align  │
                      └───────────────────┬────────────────────┘
                                          │
                      ┌───────────────────▼────────────────────┐
                      │  Virtual Memory Manager (VMM / Paging) │
                      │  Two-level paging, recursive PDE 1023  │
                      └───────────────────┬────────────────────┘
                                          │
                      ┌───────────────────▼────────────────────┐
                      │  Physical Memory Manager (PMM / Bitmap)│
                      │  Page frame allocator, E820 detection  │
                      └───────────────────┬────────────────────┘
                                          │
 ═════════════════════════════════════════╪═════════════════════════════════════════
                       HARDWARE ABSTRACTION LAYER (HAL)
 ═════════════════════════════════════════╤═════════════════════════════════════════
                                          │
           ┌──────────────────────────────┼──────────────────────────────┐
           ▼                              ▼                              ▼
┌──────────────────────┐      ┌──────────────────────┐      ┌──────────────────────┐
│  CPU & Interrupts    │      │  I/O & Video Output  │      │  Storage & Storage   │
│  • GDT / IDT / ISR   │      │  • VGA Text (0xB8000)│      │  • Stage 1 / Stage 2 │
│  • Modular i8259 PIC │      │  • Port 0xE9 Debug   │      │  • MBR Partitions    │
│  • Timer (IRQ 0)     │      │  • ANSI Logger       │      │  • FAT12/16/32 Cache │
└──────────────────────┘      └──────────────────────┘      └──────────────────────┘
```

---

## Features

### 1. Bootloader (Stage 1 & Stage 2)
- **Stage 1 MBR Boot Sector:** Linked via `linker.ld` producing a symbol map (`boot.map`); preserves the BIOS Parameter Block (BPB); forwards MBR partition entries in `DS:SI`; supports LBA INT 13h extensions; dynamically patched by `install_bootloader.py`.
- **Stage 2 Bootloader:**
  - Real-mode thunk for BIOS `INT 15h AX=0xE820` memory map detection.
  - MBR partition abstraction (`mbr.c`) with automatic fallback for unpartitioned disks.
  - FAT12, FAT16, and FAT32 driver with a **5-sector FAT cache** (`FatCache[2560]`).
  - Full **ELF32 binary loader** (`elf.c`) that validates headers, loads `PT_LOAD` segments, zeroes the `.bss` section, and jumps to `kernel.elf` entry.
  - Packs detected hardware metadata into a unified `BootParams` contract.

### 2. Kernel Hardware Abstraction Layer (HAL)
- **Modular Interrupt System:**
  - Standardized GDT (flat protected mode segments) and IDT (256 gates).
  - Modular `PICDriver` interface with `i8259` implementation and automated IRQ unmasking.
  - Hardware timer tick subscription streaming continuously.
- **Diagnostics & Virtual File System:**
  - Fast Bochs/QEMU Port 0xE9 debug console driver (`e9.c`).
  - Structured ANSI colored logger (`debug.c`) supporting `log_debug`, `log_info`, `log_warn`, `log_err`, `log_crit`.
  - Minimal VFS multiplexer routing standard I/O streams (`stdout`, `stderr`, `debug`).

### 3. Memory Management Subsystem
- **Physical Memory Manager (PMM):**
  - Bitmap-based page frame allocator tracking 4 KiB frames.
  - Bitmap placed dynamically at `PAGE_ALIGN_UP(__end)`.
  - Automatically preserves low memory (`0x00000000`–`0x00100000`), kernel code/data/BSS, and the bitmap itself.
  - High performance: 32-bit chunk scanning using GCC `__builtin_ctz` (x86 `bsf` single-cycle instruction).
  - Supports contiguous multi-page allocation (`PMM_AllocatePages`) and double-free detection.
- **Virtual Memory Manager (VMM):**
  - Two-level x86 paging hierarchy (Page Directory and Page Tables).
  - Identity maps physical RAM (128 MB = 32 Page Tables) with `PAGE_PRESENT | PAGE_WRITE`.
  - **Recursive Page Directory Self-Mapping:** Maps PDE 1023 to the Page Directory's own frame, exposing the Page Directory at `0xFFFFF000` and Page Tables at `0xFFC00000 + (K * 4096)`.
  - Dedicated Page Fault Exception (`#PF` - ISR 14) handler decoding `CR2` and error bits.
  - Dynamic on-demand page table allocation via [`VMM_MapPage`](file:///workspaces/myos/src/kernel/hal/vmm.c).
- **Kernel Heap Allocator:**
  - Virtual address space located at `0x10000000` (256 MB mark).
  - Boundary-tag doubly-linked free list allocator with strict 8-byte payload alignment.
  - Block splitting to eliminate internal fragmentation.
  - Bidirectional $O(1)$ block coalescing on `kfree` to prevent external fragmentation.
  - Automatic demand expansion (`kheap_expand`) requesting physical frames via PMM/VMM.
  - Standard primitives: `kmalloc`, `kzalloc`, `kfree`, `krealloc`.

### 4. Task Scheduling Subsystem (MLFQ)
- **Multi-Level Feedback Queue (MLFQ) Preemptive & Cooperative Scheduler:**
  - 3 priority levels with distinct time quanta:
    - **Queue 0 (High / Interactive):** 2 ticks (~20 ms)
    - **Queue 1 (Medium):** 6 ticks (~60 ms)
    - **Queue 2 (Low / Batch / CPU-bound):** 15 ticks (~150 ms)
  - Driven by the Intel 8253/8254 Programmable Interval Timer (PIT) programmed to 100 Hz.
  - Hardware context switching integrated directly into `isr.asm` (`mov esp, eax`) and `isr.c`.
  - Forged interrupt stack frames allowing new tasks to bootstrap seamlessly into `Task_Trampoline` with interrupts enabled.
  - **Rule 4b (Interactive Favoritism):** Tasks that voluntarily yield (`Task_Yield()` via `int $0x81`) before consuming their quantum retain their high priority.
  - **Rule 4a (Demotion):** Tasks consuming their entire quantum without yielding are demoted down the queue hierarchy.
  - **Rule 5 (Anti-Starvation Boost):** Every 100 ticks (~1 second), all ready tasks across all queues are elevated back to Queue 0.
  - **Deferred Stack Reaper (Suicide Paradox Solved):** Dead tasks calling `Task_Exit()` mark themselves terminated; their stacks and TCBs are reclaimed on subsequent scheduling cycles when the CPU is safely executing on another task's stack.

### 5. Input Subsystem & Interactive Command Shell (Phase 9)
- **PS/2 Keyboard Driver:**
  - Standard PS/2 controller driver (`keyboard.c`, `keyboard.h`) on ports `0x60` (data) and `0x64` (status/command).
  - Handles Scan Code Set 1 with press/release states for Left/Right Shift, Caps Lock, and Ctrl.
  - IRQ 1 handler dynamically registered on the 8259 PIC with automatic interrupt unmasking.
  - 256-byte circular FIFO ring buffer decoupling hardware interrupt rates from software consumption.
  - Non-busy-waiting key reader (`i686_Keyboard_WaitChar`) that voluntarily yields CPU (`Task_Yield()`), preserving the shell in MLFQ Queue 0 for instant interactive responsiveness.
- **VGA Text Driver Upgrades:**
  - Added in-place destructive backspace (`\b`) handling in `vga_text.c` with cursor repositioning and space clearing.
- **Interactive Command Shell:**
  - Autonomous interactive task (`shell.c`, `shell.h`) running in MLFQ Queue 0.
  - Commands:
    - `help`: Lists all available commands.
    - `clear`: Clears VGA screen and resets cursor position.
    - `ps`: Live table of all active tasks, states, MLFQ queue levels, consumed ticks, yield counts, and preemption counts.
    - `mem`: Detailed memory report across Physical (PMM), Virtual (VMM), and Kernel Heap subsystems.
    - `spawn <name> [interactive|compute]`: Dynamically allocates a new worker task and queues it in the MLFQ scheduler.
    - `kill <pid>`: Dynamically terminates a running task and routes it to the deferred stack reaper.
    - `stats`: Live MLFQ scheduler metrics.
    - `echo <text...>`: Echoes arguments to terminal and Port 0xE9.
    - `reboot`: Sends 8042 keyboard controller CPU reset pulse to reboot the machine.

---

## Memory Map Layout

```
0x00000000 ────────┐
                   │  Lower 1 MB: BIOS IVT, BDA, EBDA, VGA Text (0xB8000), Stage 1/2
0x00100000 ────────┼──
                   │  Kernel ELF Executable (.text, .rodata, .data, .bss)
0x00107690 ────────┼──
                   │  PMM Frame Tracking Bitmap (4 KiB)
0x00108000 ────────┼──
                   │  Kernel Page Directory & Identity-Mapped Page Tables
                   │  Usable Physical RAM (Identity-mapped up to 128 MB)
0x08000000 ────────┴──
                   ...
0x10000000 ────────┬── [KERNEL HEAP BASE (256 MB)]
                   │   Initial heap: 1 MiB (256 pages)
                   │   Expands dynamically up to 64 MiB
0x14000000 ────────┴── [KERNEL HEAP CEILING (320 MB)]
                   ...
0xFFC00000 ────────┬── Recursive Page Tables Window
0xFFFFF000 ────────┴── Recursive Page Directory (Self-Mapped PDE 1023)
```

---

## Project Structure

```
myos/
├── CMakeLists.txt              # Root CMake build file
├── Makefile                    # Make bridge for toolchain and CMake targets
├── OS_PROGRESS_TRACKER.md      # Detailed milestone and architectural tracker
├── build_scripts/
│   ├── install_bootloader.py   # Injects Stage 2 sector metadata into Stage 1 BPB
│   ├── generate_isrs.sh        # Generates assembly exception gates
│   └── setup_toolchain.sh      # Automates i686-elf cross-compiler build
├── cmake/
│   └── toolchain.cmake         # CMake toolchain configuration
├── src/
│   ├── bootloader/
│   │   ├── stage1/             # MBR boot sector (boot.asm, linker.ld)
│   │   └── stage2/             # 32-bit loader (E820, MBR, FAT12/16/32, ELF32)
│   ├── kernel/
│   │   ├── arch/i686/          # x86 CPU hardware drivers (GDT, IDT, PIC, VMM assembly)
│   │   ├── hal/                # Hardware Abstraction Layer (PMM, VMM, Heap, VFS)
│   │   ├── main.c              # Kernel entry point and test suite
│   │   └── linker.ld           # elf32-i386 kernel linker script
│   └── libs/
│       └── boot/
│           └── bootparams.h    # Shared Stage 2 <-> Kernel boot data contract
└── tools/
    └── fat/                    # Host FAT manipulation utilities
```

---

## Getting Started

### Prerequisites
- **Linux** or **Docker / Devcontainer** (recommended)
- **i686-elf cross-compiler** (GCC 11.2.0, binutils)
- **CMake** (>= 3.10)
- **NASM** (>= 2.15)
- **QEMU** (`qemu-system-i386`)
- **mtools** (`mkfs.fat`, `mcopy`, `mmd`)

### 1. Build the Toolchain
If you haven't built the cross-compiler yet, run:
```bash
make toolchain
```
This automatically compiles `i686-elf-gcc` and `i686-elf-binutils` into `./toolchain/`.

### 2. Build the Operating System
```bash
make build
```
This configures CMake, compiles the bootloader stages, compiles the kernel ELF with global constructors (`crti.o`/`crtn.o`), creates the bootable disk image, and patches the boot sector.

### 3. Run in QEMU
```bash
make run
```
Or run directly with Port 0xE9 debug console redirection:
```bash
qemu-system-i386 -drive file=build/main_floppy.img,format=raw,if=ide -debugcon stdio
```

---

## Verification & Progress Tracking
All architectural milestones, design specifications, bug fixes, and verification logs are documented in [`OS_PROGRESS_TRACKER.md`](OS_PROGRESS_TRACKER.md).

---

## License
This project is licensed for educational and developmental purposes.
