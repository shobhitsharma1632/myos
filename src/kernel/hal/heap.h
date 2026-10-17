#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define KERNEL_HEAP_START           0x10000000  // 256 MiB mark
#define KERNEL_HEAP_INITIAL_SIZE    0x00100000  // 1 MiB
#define KERNEL_HEAP_MAX_SIZE        0x04000000  // 64 MiB

#define HEAP_MAGIC                  0x1EA91337
#define HEAP_MIN_SPLIT_SIZE         16

typedef struct BlockHeader BlockHeader;

struct BlockHeader {
    uint32_t magic;
    size_t size;
    bool is_free;
    uint8_t reserved[3];     // Align to 4-byte boundary
    BlockHeader* next;
    BlockHeader* prev;
    uint32_t padding;        // Pad structure to 24 bytes (multiple of 8)
};

// Initialize the Kernel Dynamic Memory Allocator
void KHeap_Initialize(void);

// Standard dynamic memory allocation primitives
void* kmalloc(size_t size);
void* kzalloc(size_t size);
void  kfree(void* ptr);
void* krealloc(void* ptr, size_t new_size);

// Heap statistics
size_t KHeap_GetTotalMemory(void);
size_t KHeap_GetUsedMemory(void);
size_t KHeap_GetFreeMemory(void);
