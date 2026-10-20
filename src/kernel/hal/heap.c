#include "heap.h"
#include "vmm.h"
#include "pmm.h"
#include "memory.h"
#include <debug.h>
#include <stdio.h>

static BlockHeader* g_HeapStart = NULL;
static uint32_t     g_HeapVirtualEnd = KERNEL_HEAP_START;
static size_t       g_HeapTotalBytes = 0;
static size_t       g_HeapUsedBytes = 0;
static size_t       g_HeapFreeBytes = 0;

static bool kheap_expand(size_t required_bytes)
{
    size_t pages = (required_bytes + PAGE_SIZE - 1) / PAGE_SIZE;

    if (g_HeapVirtualEnd + (pages * PAGE_SIZE) > KERNEL_HEAP_START + KERNEL_HEAP_MAX_SIZE)
    {
        log_err("Heap", "kheap_expand: Maximum heap limit (64 MB) reached!");
        return false;
    }

    log_info("Heap", "Expanding heap by %u pages (%u KB)...",
             (uint32_t)pages, (uint32_t)(pages * PAGE_SIZE) / 1024);

    for (size_t i = 0; i < pages; i++)
    {
        void* phys = PMM_AllocatePage();
        if (!phys)
        {
            log_err("Heap", "kheap_expand: Physical memory exhausted!");
            return false;
        }

        if (!VMM_MapPage((void*)g_HeapVirtualEnd, phys, PAGE_PRESENT | PAGE_WRITE))
        {
            log_err("Heap", "kheap_expand: VMM_MapPage failed at 0x%x!", g_HeapVirtualEnd);
            PMM_FreePage(phys);
            return false;
        }

        g_HeapVirtualEnd += PAGE_SIZE;
        g_HeapTotalBytes += PAGE_SIZE;
    }

    // Integrate newly mapped memory into the free list
    if (g_HeapStart == NULL)
    {
        g_HeapStart = (BlockHeader*)KERNEL_HEAP_START;
        g_HeapStart->magic = HEAP_MAGIC;
        g_HeapStart->size = (pages * PAGE_SIZE) - sizeof(BlockHeader);
        g_HeapStart->is_free = true;
        g_HeapStart->prev = NULL;
        g_HeapStart->next = NULL;

        g_HeapFreeBytes += g_HeapStart->size;
        g_HeapUsedBytes += sizeof(BlockHeader);
    }
    else
    {
        // Find trailing block in list
        BlockHeader* last = g_HeapStart;
        while (last->next != NULL)
            last = last->next;

        if (last->is_free)
        {
            last->size += (pages * PAGE_SIZE);
            g_HeapFreeBytes += (pages * PAGE_SIZE);
        }
        else
        {
            BlockHeader* new_block = (BlockHeader*)((uint8_t*)(last + 1) + last->size);
            new_block->magic = HEAP_MAGIC;
            new_block->size = (pages * PAGE_SIZE) - sizeof(BlockHeader);
            new_block->is_free = true;
            new_block->prev = last;
            new_block->next = NULL;
            last->next = new_block;

            g_HeapFreeBytes += new_block->size;
            g_HeapUsedBytes += sizeof(BlockHeader);
        }
    }

    return true;
}

void KHeap_Initialize(void)
{
    log_info("Heap", "Initializing Kernel Dynamic Memory Allocator (Heap)...");
    log_info("Heap", "Heap Virtual Base: 0x%x | Initial Size: %u KB | Max Size: %u MB",
             KERNEL_HEAP_START, KERNEL_HEAP_INITIAL_SIZE / 1024, KERNEL_HEAP_MAX_SIZE / (1024 * 1024));

    if (!kheap_expand(KERNEL_HEAP_INITIAL_SIZE))
    {
        log_crit("Heap", "Failed to allocate initial kernel heap!");
        return;
    }

    log_info("Heap", "Heap initialized: %u KB free, %u bytes overhead",
             (uint32_t)(g_HeapFreeBytes / 1024), (uint32_t)g_HeapUsedBytes);
}

void* kmalloc(size_t size)
{
    if (size == 0)
        return NULL;

    // 8-byte alignment
    size = (size + 7) & ~7;
    if (size < HEAP_MIN_SPLIT_SIZE)
        size = HEAP_MIN_SPLIT_SIZE;

    BlockHeader* curr = g_HeapStart;
    while (curr != NULL)
    {
        if (curr->magic != HEAP_MAGIC)
        {
            log_crit("Heap", "kmalloc: Heap corruption detected at 0x%x (magic 0x%x)!",
                     (uint32_t)curr, curr->magic);
            return NULL;
        }

        if (curr->is_free && curr->size >= size)
        {
            // Suitable free block found: check if splitting is beneficial
            if (curr->size >= size + sizeof(BlockHeader) + HEAP_MIN_SPLIT_SIZE)
            {
                BlockHeader* remainder = (BlockHeader*)((uint8_t*)(curr + 1) + size);
                remainder->magic = HEAP_MAGIC;
                remainder->size = curr->size - size - sizeof(BlockHeader);
                remainder->is_free = true;
                remainder->prev = curr;
                remainder->next = curr->next;
                if (curr->next)
                    curr->next->prev = remainder;
                curr->next = remainder;

                curr->size = size;
                g_HeapUsedBytes += sizeof(BlockHeader);
                g_HeapFreeBytes -= sizeof(BlockHeader);
            }

            curr->is_free = false;
            g_HeapUsedBytes += curr->size;
            g_HeapFreeBytes -= curr->size;

            log_debug("Heap", "kmalloc(%u) -> 0x%x [hdr=0x%x]", (uint32_t)size, (uint32_t)(curr + 1), (uint32_t)curr);
            return (void*)(curr + 1);
        }

        curr = curr->next;
    }

    // No free block fits: attempt dynamic heap expansion
    size_t needed = size + sizeof(BlockHeader);
    if (kheap_expand(needed))
    {
        return kmalloc(size);
    }

    log_err("Heap", "kmalloc: Out of memory! Failed to allocate %u bytes", (uint32_t)size);
    return NULL;
}

void kfree(void* ptr)
{
    if (!ptr)
        return;

    BlockHeader* block = (BlockHeader*)ptr - 1;
    if (block->magic != HEAP_MAGIC)
    {
        log_crit("Heap", "kfree: Invalid block header magic 0x%x at 0x%x!",
                 block->magic, (uint32_t)block);
        return;
    }

    if (block->is_free)
    {
        log_warn("Heap", "kfree: Double free detected on block 0x%x!", (uint32_t)block);
        return;
    }

    block->is_free = true;
    g_HeapUsedBytes -= block->size;
    g_HeapFreeBytes += block->size;
    log_debug("Heap", "kfree(0x%x) [hdr=0x%x, sz=%u]", (uint32_t)ptr, (uint32_t)block, (uint32_t)block->size);

    // O(1) Immediate coalesce with next block if free
    if (block->next && block->next->is_free && block->next->magic == HEAP_MAGIC)
    {
        block->size += sizeof(BlockHeader) + block->next->size;
        block->next = block->next->next;
        if (block->next)
            block->next->prev = block;
        g_HeapUsedBytes -= sizeof(BlockHeader);
        g_HeapFreeBytes += sizeof(BlockHeader);
    }

    // O(1) Immediate coalesce with previous block if free
    if (block->prev && block->prev->is_free && block->prev->magic == HEAP_MAGIC)
    {
        block->prev->size += sizeof(BlockHeader) + block->size;
        block->prev->next = block->next;
        if (block->next)
            block->next->prev = block->prev;
        g_HeapUsedBytes -= sizeof(BlockHeader);
        g_HeapFreeBytes += sizeof(BlockHeader);
    }
}

void* kzalloc(size_t size)
{
    void* ptr = kmalloc(size);
    if (ptr)
        memset(ptr, 0, size);
    return ptr;
}

void* krealloc(void* ptr, size_t new_size)
{
    if (!ptr)
        return kmalloc(new_size);

    if (new_size == 0)
    {
        kfree(ptr);
        return NULL;
    }

    BlockHeader* block = (BlockHeader*)ptr - 1;
    if (block->magic != HEAP_MAGIC)
    {
        log_crit("Heap", "krealloc: Invalid block magic 0x%x at 0x%x!",
                 block->magic, (uint32_t)block);
        return NULL;
    }

    if (new_size <= block->size)
        return ptr;

    void* new_ptr = kmalloc(new_size);
    if (!new_ptr)
        return NULL;

    memcpy(new_ptr, ptr, block->size);
    kfree(ptr);
    return new_ptr;
}

size_t KHeap_GetTotalMemory(void)
{
    return g_HeapTotalBytes;
}

size_t KHeap_GetUsedMemory(void)
{
    return g_HeapUsedBytes;
}

size_t KHeap_GetFreeMemory(void)
{
    return g_HeapFreeBytes;
}
