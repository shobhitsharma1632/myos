#pragma once
#include <stdint.h>

#define i686_GDT_CODE_SEGMENT       0x08
#define i686_GDT_DATA_SEGMENT       0x10
#define i686_GDT_USER_CODE_SEGMENT  0x18
#define i686_GDT_USER_DATA_SEGMENT  0x20
#define i686_GDT_TSS_SEGMENT        0x28

#define GDT_TSS_BASE_INDEX          5
#define GDT_TSS_SELECTOR_FOR_CPU(cpu_id) ((uint16_t)(((GDT_TSS_BASE_INDEX) + (cpu_id)) * 8))

// Selectors with Requested Privilege Level (RPL)
#define KERNEL_CODE_SELECTOR        i686_GDT_CODE_SEGMENT          // 0x08 (RPL 0)
#define KERNEL_DATA_SELECTOR        i686_GDT_DATA_SEGMENT          // 0x10 (RPL 0)
#define USER_CODE_SELECTOR          (i686_GDT_USER_CODE_SEGMENT | 3) // 0x1B (RPL 3)
#define USER_DATA_SELECTOR          (i686_GDT_USER_DATA_SEGMENT | 3) // 0x23 (RPL 3)
#define TSS_SELECTOR                i686_GDT_TSS_SEGMENT           // 0x28 (RPL 0)

void i686_GDT_Initialize(void);
void i686_GDT_SetEntry(int index, uint32_t base, uint32_t limit, uint8_t access, uint8_t flags);
void __attribute__((cdecl)) i686_LTR(uint16_t selector);