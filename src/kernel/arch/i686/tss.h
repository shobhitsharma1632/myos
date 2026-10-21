#pragma once
#include <stdint.h>

// Standard x86 32-bit Task State Segment (TSS) Structure
typedef struct {
    uint32_t prev_tss;   // Previous TSS link (unused in software task switching)
    uint32_t esp0;       // Kernel stack pointer loaded upon privilege transition (Ring 3 -> Ring 0)
    uint32_t ss0;        // Kernel stack segment selector (0x10)
    uint32_t esp1;       // Unused Ring 1 stack pointer
    uint32_t ss1;
    uint32_t esp2;       // Unused Ring 2 stack pointer
    uint32_t ss2;
    uint32_t cr3;        // Page directory base register
    uint32_t eip;        // Saved instruction pointer
    uint32_t eflags;     // Saved processor flags
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es;
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base; // Offset to I/O Permission Bit Map (set to sizeof(TSS) for none)
} __attribute__((packed)) TSS;

// Public TSS Subsystem API
void TSS_Initialize(uint16_t kernelDataSegment);
void TSS_InitializeAP(uint32_t cpu_id, uint16_t kernelDataSegment);
void TSS_SetKernelStackForCPU(uint32_t cpu_id, uint32_t esp0);
uint32_t TSS_GetKernelStackForCPU(uint32_t cpu_id);
void TSS_SetKernelStack(uint32_t esp0);
uint32_t TSS_GetKernelStack(void);
