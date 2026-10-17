#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <boot/bootparams.h>

#define PAGE_SIZE               4096
#define PAGE_SHIFT              12
#define PAGE_MASK               (PAGE_SIZE - 1)

#define PAGE_ALIGN_DOWN(addr)   ((uint32_t)(addr) & ~PAGE_MASK)
#define PAGE_ALIGN_UP(addr)     (((uint32_t)(addr) + PAGE_MASK) & ~PAGE_MASK)

// Initialize the Physical Memory Manager using the BIOS E820 memory map
void PMM_Initialize(const BootParams* bootParams);

// Allocate a single 4 KiB physical page frame
void* PMM_AllocatePage(void);

// Allocate contiguous physical page frames
void* PMM_AllocatePages(size_t count);

// Free a single 4 KiB physical page frame
void PMM_FreePage(void* ptr);

// Free contiguous physical page frames
void PMM_FreePages(void* ptr, size_t count);

// Memory statistics and status queries
uint32_t PMM_GetTotalMemory(void);
uint32_t PMM_GetFreeMemory(void);
uint32_t PMM_GetUsedMemory(void);
uint32_t PMM_GetTotalFrames(void);
uint32_t PMM_GetFreeFrames(void);
