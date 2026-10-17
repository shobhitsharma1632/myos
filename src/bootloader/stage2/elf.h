#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "mbr.h"

#define ELF_MAGIC ("\x7F" "ELF")

typedef struct 
{
    uint8_t Magic[4];
    uint8_t Bitness;            // 1 = 32 bit, 2 = 64 bit
    uint8_t Endianness;         // 1 = little endian, 2 = big endian
    uint8_t ELFHeaderVersion;
    uint8_t ABI;
    uint8_t _Padding[8];
    uint16_t Type;              // 1 = relocatable, 2 = executable, 3 = shared, 4 = core
    uint16_t InstructionSet;
    uint32_t ELFVersion;
    uint32_t ProgramEntryPosition;
    uint32_t ProgramHeaderTablePosition;
    uint32_t SectionHeaderTablePosition;
    uint32_t Flags;
    uint16_t HeaderSize;
    uint16_t ProgramHeaderTableEntrySize;
    uint16_t ProgramHeaderTableEntryCount;
    uint16_t SectionHeaderTableEntrySize;
    uint16_t SectionHeaderTableEntryCount;
    uint16_t SectionNamesIndex;

} __attribute__((packed)) ELFHeader;

enum ELFBitness 
{
    ELF_BITNESS_32BIT           = 1,
    ELF_BITNESS_64BIT           = 2,
};

enum ELFEndianness 
{
    ELF_ENDIANNESS_LITTLE       = 1,
    ELF_ENDIANNESS_BIG          = 2,
};

enum ELFInstructionSet
{
    ELF_INSTRUCTION_SET_NONE    = 0,
    ELF_INSTRUCTION_SET_X86     = 3,
    ELF_INSTRUCTION_SET_ARM     = 0x28,
    ELF_INSTRUCTION_SET_X64     = 0x3E,
    ELF_INSTRUCTION_SET_ARM64   = 0xB7,
    ELF_INSTRUCTION_SET_RISCV   = 0xF3,
};

enum ELFType
{
    ELF_TYPE_RELOCATABLE        = 1,
    ELF_TYPE_EXECUTABLE         = 2,
    ELF_TYPE_SHARED             = 3,
    ELF_TYPE_CORE               = 4,
};

typedef struct
{
    uint32_t Type;
    uint32_t Offset;
    uint32_t VirtualAddress;
    uint32_t PhysicalAddress;
    uint32_t FileSize;
    uint32_t MemorySize;
    uint32_t Flags;
    uint32_t Align;

} __attribute__((packed)) ELFProgramHeader;

enum ELFProgramType {
    ELF_PROGRAM_TYPE_NULL           = 0,
    ELF_PROGRAM_TYPE_LOAD           = 1,
    ELF_PROGRAM_TYPE_DYNAMIC        = 2,
    ELF_PROGRAM_TYPE_INTERP         = 3,
    ELF_PROGRAM_TYPE_NOTE           = 4,
    ELF_PROGRAM_TYPE_SHLIB          = 5,
    ELF_PROGRAM_TYPE_PHDR           = 6,
    ELF_PROGRAM_TYPE_TLS            = 7,
};

bool ELF_Read(Partition* disk, const char* path, void** entryPoint);
