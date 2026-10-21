#include "tss.h"
#include "gdt.h"
#include <memory.h>
#include <debug.h>

#define MODULE "TSS"
#define MAX_TSS_CPUS 16

static TSS g_CPU_TSS[MAX_TSS_CPUS];

void TSS_InitializeAP(uint32_t cpu_id, uint16_t kernelDataSegment)
{
    if (cpu_id >= MAX_TSS_CPUS) return;

    memset(&g_CPU_TSS[cpu_id], 0, sizeof(TSS));

    g_CPU_TSS[cpu_id].ss0 = kernelDataSegment; // Kernel Data Segment 0x10
    g_CPU_TSS[cpu_id].esp0 = 0;                // Updated dynamically per task by Task_Schedule
    g_CPU_TSS[cpu_id].iomap_base = sizeof(TSS); // No I/O permissions bitmap

    uint16_t selector = GDT_TSS_SELECTOR_FOR_CPU(cpu_id);
    int gdt_index = GDT_TSS_BASE_INDEX + (int)cpu_id;

    // Install TSS descriptor in GDT
    // Base: &g_CPU_TSS[cpu_id], Limit: sizeof(TSS) - 1, Access: 0x89, Flags: 0x00
    i686_GDT_SetEntry(gdt_index, (uint32_t)&g_CPU_TSS[cpu_id], sizeof(TSS) - 1, 0x89, 0x00);

    // Load Task Register (TR)
    i686_LTR(selector);

    log_info(MODULE, "TSS for CPU %u initialized at 0x%x (GDT index %d), TR loaded with selector 0x%x.",
             cpu_id, (uint32_t)&g_CPU_TSS[cpu_id], gdt_index, selector);
}

void TSS_Initialize(uint16_t kernelDataSegment)
{
    TSS_InitializeAP(0, kernelDataSegment);
}

void TSS_SetKernelStackForCPU(uint32_t cpu_id, uint32_t esp0)
{
    if (cpu_id < MAX_TSS_CPUS)
    {
        g_CPU_TSS[cpu_id].esp0 = esp0;
    }
}

uint32_t TSS_GetKernelStackForCPU(uint32_t cpu_id)
{
    if (cpu_id < MAX_TSS_CPUS)
    {
        return g_CPU_TSS[cpu_id].esp0;
    }
    return 0;
}

void TSS_SetKernelStack(uint32_t esp0)
{
    extern uint32_t SMP_GetCurrentCPUID(void);
    uint32_t cpu_id = SMP_GetCurrentCPUID();
    TSS_SetKernelStackForCPU(cpu_id, esp0);
}

uint32_t TSS_GetKernelStack(void)
{
    extern uint32_t SMP_GetCurrentCPUID(void);
    uint32_t cpu_id = SMP_GetCurrentCPUID();
    return TSS_GetKernelStackForCPU(cpu_id);
}
