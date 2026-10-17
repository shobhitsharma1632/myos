#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <boot/bootparams.h>

// Page Table / Directory Entry Flags
#define PAGE_PRESENT            0x001
#define PAGE_WRITE              0x002
#define PAGE_USER               0x004
#define PAGE_WRITE_THROUGH      0x008
#define PAGE_CACHE_DISABLE      0x010
#define PAGE_ACCESSED           0x020
#define PAGE_DIRTY              0x040
#define PAGE_SIZE_4MB           0x080
#define PAGE_GLOBAL             0x100

// Self-mapping Virtual Addresses (via recursive PDE 1023)
#define VMM_PAGE_DIRECTORY_VIRT ((uint32_t*)0xFFFFF000)
#define VMM_PAGE_TABLE_VIRT(i)  ((uint32_t*)(0xFFC00000 + ((uint32_t)(i) * 4096)))

// Architecture Assembly Helpers
void     i686_VMM_LoadPageDirectory(uint32_t pd_phys);
uint32_t i686_VMM_GetPageDirectory(void);
void     i686_VMM_EnablePaging(void);
void     i686_VMM_DisablePaging(void);
void     i686_VMM_FlushTLB(uint32_t vaddr);
uint32_t i686_VMM_GetFaultAddress(void);
bool     i686_VMM_IsPagingEnabled(void);

// VMM Interface
void VMM_Initialize(const BootParams* bootParams);
bool VMM_MapPage(void* virtAddr, void* physAddr, uint32_t flags);
void VMM_UnmapPage(void* virtAddr);
bool VMM_GetPhysicalAddress(void* virtAddr, void** outPhysAddr);
bool VMM_IsMapped(void* virtAddr);
