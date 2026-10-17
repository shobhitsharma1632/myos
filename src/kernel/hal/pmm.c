#include "pmm.h"
#include <debug.h>
#include "memory.h"

// Symbol exported from linker script (linker.ld) marking kernel end
extern uint8_t __end;

// Pointer to bitmap array: 1 bit per 4 KiB frame (0 = free, 1 = used/reserved)
static uint32_t* g_Bitmap = NULL;
static uint32_t  g_TotalFrames = 0;
static uint32_t  g_FreeFrames = 0;
static uint32_t  g_TotalMemoryBytes = 0;
static uint32_t  g_BitmapPages = 0;

static inline void pmm_bitmap_set(uint32_t frame)
{
    g_Bitmap[frame / 32] |= (1U << (frame % 32));
}

static inline void pmm_bitmap_clear(uint32_t frame)
{
    g_Bitmap[frame / 32] &= ~(1U << (frame % 32));
}

static inline bool pmm_bitmap_test(uint32_t frame)
{
    return (g_Bitmap[frame / 32] & (1U << (frame % 32))) != 0;
}

void PMM_Initialize(const BootParams* bootParams)
{
    log_info("PMM", "Initializing Physical Memory Manager...");

    if (!bootParams || bootParams->Memory.RegionCount <= 0)
    {
        log_crit("PMM", "Invalid boot parameters or empty memory map!");
        return;
    }

    // 1. Calculate the highest usable physical address from BIOS E820 map
    uint64_t maxAddress = 0;
    for (int i = 0; i < bootParams->Memory.RegionCount; i++)
    {
        const MemoryRegion* reg = &bootParams->Memory.Regions[i];
        if (reg->Type == MEMORY_TYPE_USABLE)
        {
            uint64_t regEnd = reg->Begin + reg->Length;
            if (regEnd > maxAddress)
                maxAddress = regEnd;
        }
    }

    // In 32-bit x86 protected mode, clamp to 4 GB address space
    if (maxAddress > 0xFFFFFFFFULL)
        maxAddress = 0x100000000ULL;

    g_TotalMemoryBytes = (uint32_t)maxAddress;
    g_TotalFrames = g_TotalMemoryBytes / PAGE_SIZE;

    // 2. Position the bitmap immediately after the kernel binary (__end) aligned to 4 KiB
    uint32_t kernelEnd = (uint32_t)&__end;
    g_Bitmap = (uint32_t*)PAGE_ALIGN_UP(kernelEnd);

    // Number of bytes required: 1 bit per page frame
    uint32_t bitmapBytes = (g_TotalFrames + 7) / 8;
    g_BitmapPages = PAGE_ALIGN_UP(bitmapBytes) / PAGE_SIZE;
    uint32_t bitmapEnd = (uint32_t)g_Bitmap + (g_BitmapPages * PAGE_SIZE);

    log_info("PMM", "Kernel ceiling: 0x%x | Bitmap at: 0x%x - 0x%x (%u KB, %u pages)",
             kernelEnd, (uint32_t)g_Bitmap, bitmapEnd,
             (g_BitmapPages * PAGE_SIZE) / 1024, g_BitmapPages);

    // 3. Mark ALL frames as used/reserved by default (safety first)
    memset(g_Bitmap, 0xFF, g_BitmapPages * PAGE_SIZE);
    g_FreeFrames = 0;

    // 4. Mark all E820 usable RAM regions as free
    for (int i = 0; i < bootParams->Memory.RegionCount; i++)
    {
        const MemoryRegion* reg = &bootParams->Memory.Regions[i];
        if (reg->Type != MEMORY_TYPE_USABLE)
            continue;

        // Skip any memory region that starts beyond 4 GB
        if (reg->Begin >= 0x100000000ULL)
            continue;

        uint32_t startFrame = (uint32_t)(PAGE_ALIGN_UP(reg->Begin) / PAGE_SIZE);
        uint64_t endAddr = reg->Begin + reg->Length;
        if (endAddr > 0x100000000ULL)
            endAddr = 0x100000000ULL;

        uint32_t endFrame = (uint32_t)(PAGE_ALIGN_DOWN(endAddr) / PAGE_SIZE);
        if (endFrame > g_TotalFrames)
            endFrame = g_TotalFrames;

        for (uint32_t f = startFrame; f < endFrame; f++)
        {
            if (pmm_bitmap_test(f))
            {
                pmm_bitmap_clear(f);
                g_FreeFrames++;
            }
        }
    }

    // 5. Re-reserve critical regions:
    // Frames [0 .. reservedEndFrame)
    // - Lower 1 MB: 0x00000000 - 0x00100000 (BIOS IVT, BDA, EBDA, VGA 0xA0000, Stage 1/2 stacks)
    // - Kernel ELF: 0x00100000 - kernelEnd (.text, .data, .rodata, .bss)
    // - PMM Bitmap: kernelEnd - bitmapEnd
    uint32_t reservedEndFrame = PAGE_ALIGN_UP(bitmapEnd) / PAGE_SIZE;
    for (uint32_t f = 0; f < reservedEndFrame && f < g_TotalFrames; f++)
    {
        if (!pmm_bitmap_test(f))
        {
            pmm_bitmap_set(f);
            g_FreeFrames--;
        }
    }

    // 6. Ensure any padding bits in the last dword beyond g_TotalFrames remain set (reserved)
    uint32_t totalDwords = (g_TotalFrames + 31) / 32;
    for (uint32_t f = g_TotalFrames; f < totalDwords * 32; f++)
    {
        pmm_bitmap_set(f);
    }

    log_info("PMM", "Total Memory: %u MB (%u frames)",
             g_TotalMemoryBytes / (1024 * 1024), g_TotalFrames);
    log_info("PMM", "Free Memory:  %u MB (%u frames)",
             (g_FreeFrames * PAGE_SIZE) / (1024 * 1024), g_FreeFrames);
    log_info("PMM", "Used Memory:  %u KB (%u frames)",
             ((g_TotalFrames - g_FreeFrames) * PAGE_SIZE) / 1024,
             g_TotalFrames - g_FreeFrames);
}

void* PMM_AllocatePage(void)
{
    if (g_FreeFrames == 0)
    {
        log_err("PMM", "Out of physical memory!");
        return NULL;
    }

    uint32_t totalDwords = (g_TotalFrames + 31) / 32;
    for (uint32_t i = 0; i < totalDwords; i++)
    {
        if (g_Bitmap[i] != 0xFFFFFFFF)
        {
            // Found a dword with at least one free frame (0 bit)
            uint32_t bit = __builtin_ctz(~g_Bitmap[i]);
            uint32_t frame = i * 32 + bit;
            if (frame >= g_TotalFrames)
                return NULL;

            pmm_bitmap_set(frame);
            g_FreeFrames--;
            return (void*)(frame * PAGE_SIZE);
        }
    }

    log_err("PMM", "No free frame found despite non-zero counter!");
    return NULL;
}

void* PMM_AllocatePages(size_t count)
{
    if (count == 0)
        return NULL;

    if (count == 1)
        return PMM_AllocatePage();

    if (count > g_FreeFrames)
    {
        log_err("PMM", "Cannot allocate %u pages: only %u free", (uint32_t)count, g_FreeFrames);
        return NULL;
    }

    uint32_t runStart = 0;
    size_t runLength = 0;

    for (uint32_t frame = 0; frame < g_TotalFrames; frame++)
    {
        // Fast-skip fully allocated 32-bit blocks when not currently tracking a run
        if (runLength == 0 && (frame % 32 == 0) && (g_Bitmap[frame / 32] == 0xFFFFFFFF))
        {
            frame += 31; // loop increment adds +1, skipping entire 32-bit chunk
            continue;
        }

        if (!pmm_bitmap_test(frame))
        {
            if (runLength == 0)
                runStart = frame;

            runLength++;
            if (runLength == count)
            {
                // Found contiguous block of 'count' free frames!
                for (uint32_t f = runStart; f < runStart + count; f++)
                {
                    pmm_bitmap_set(f);
                }
                g_FreeFrames -= count;
                return (void*)(runStart * PAGE_SIZE);
            }
        }
        else
        {
            runLength = 0;
        }
    }

    log_warn("PMM", "Failed to allocate %u contiguous frames (fragmentation)", (uint32_t)count);
    return NULL;
}

void PMM_FreePage(void* ptr)
{
    uint32_t addr = (uint32_t)ptr;
    if (addr & PAGE_MASK)
    {
        log_err("PMM", "PMM_FreePage: unaligned physical address 0x%x", addr);
        return;
    }

    uint32_t frame = addr >> PAGE_SHIFT;
    if (frame >= g_TotalFrames)
    {
        log_err("PMM", "PMM_FreePage: frame %u out of bounds (max %u)", frame, g_TotalFrames);
        return;
    }

    if (!pmm_bitmap_test(frame))
    {
        log_warn("PMM", "PMM_FreePage: double free on frame %u (addr 0x%x)", frame, addr);
        return;
    }

    pmm_bitmap_clear(frame);
    g_FreeFrames++;
}

void PMM_FreePages(void* ptr, size_t count)
{
    uint32_t addr = (uint32_t)ptr;
    if (addr & PAGE_MASK)
    {
        log_err("PMM", "PMM_FreePages: unaligned physical address 0x%x", addr);
        return;
    }

    for (size_t i = 0; i < count; i++)
    {
        PMM_FreePage((void*)(addr + i * PAGE_SIZE));
    }
}

uint32_t PMM_GetTotalMemory(void)
{
    return g_TotalMemoryBytes;
}

uint32_t PMM_GetFreeMemory(void)
{
    return g_FreeFrames * PAGE_SIZE;
}

uint32_t PMM_GetUsedMemory(void)
{
    return (g_TotalFrames - g_FreeFrames) * PAGE_SIZE;
}

uint32_t PMM_GetTotalFrames(void)
{
    return g_TotalFrames;
}

uint32_t PMM_GetFreeFrames(void)
{
    return g_FreeFrames;
}
