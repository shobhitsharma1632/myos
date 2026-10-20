#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define ELF_MAGIC ("\x7F" "ELF")

typedef struct {
    uint8_t  magic[4];
    uint8_t  bitness;            // 1 = 32 bit, 2 = 64 bit
    uint8_t  endianness;         // 1 = little endian, 2 = big endian
    uint8_t  version;
    uint8_t  abi;
    uint8_t  padding[8];
    uint16_t type;               // 1 = relocatable, 2 = executable
    uint16_t machine;            // 3 = x86 (EM_386)
    uint32_t version2;
    uint32_t entry;              // Program virtual entry point
    uint32_t phoff;              // Program header table offset
    uint32_t shoff;              // Section header table offset
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} __attribute__((packed)) Elf32_Ehdr;

typedef struct {
    uint32_t type;               // 1 = PT_LOAD
    uint32_t offset;             // File offset
    uint32_t vaddr;              // Virtual target address
    uint32_t paddr;              // Physical target address (ignored)
    uint32_t filesz;             // Size of segment in file
    uint32_t memsz;              // Size of segment in memory (memsz >= filesz)
    uint32_t flags;              // 1 = Executable, 2 = Writable, 4 = Readable
    uint32_t align;
} __attribute__((packed)) Elf32_Phdr;

#define PT_LOAD                 1
#define PF_X                    1
#define PF_W                    2
#define PF_R                    4

#define ELF_MAX_TRACKED_PAGES   256
#define ELF_MAX_TRACKED_PTS     32

#define USER_STACK_TOP          0x40100000 // 1 GiB + 1 MiB boundary
#define USER_STACK_PAGES        4          // 16 KiB user stack

typedef struct {
    uint32_t entry_point;
    uint32_t page_directory_phys;
    uint32_t user_stack_top;
    uint32_t user_heap_break;
    size_t   pages_count;
    void*    pages_phys[ELF_MAX_TRACKED_PAGES];
    void*    pages_virt[ELF_MAX_TRACKED_PAGES];
    size_t   pts_count;
    void*    pts_phys[ELF_MAX_TRACKED_PTS];
} ELF_ProgramImage;

bool ELF_Load(const char* filepath, ELF_ProgramImage* outImage);
void ELF_Unload(ELF_ProgramImage* image);
