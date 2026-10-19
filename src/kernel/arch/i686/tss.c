#include "tss.h"
#include "gdt.h"
#include <memory.h>
#include <debug.h>

#define MODULE "TSS"

static TSS g_TSS;

void TSS_Initialize(uint16_t kernelDataSegment)
{
    memset(&g_TSS, 0, sizeof(TSS));

    g_TSS.ss0 = kernelDataSegment; // Kernel Data Segment 0x10
    g_TSS.esp0 = 0;                // Updated dynamically per task by Task_Schedule
    g_TSS.iomap_base = sizeof(TSS); // No I/O permissions bitmap (denies direct port I/O from Ring 3)

    // Install TSS descriptor at GDT index 5 (selector 0x28)
    // Base: &g_TSS, Limit: sizeof(TSS) - 1, Access: 0x89 (Present, Ring 0 DPL, 32-bit TSS available), Flags: 0x00
    i686_GDT_SetEntry(5, (uint32_t)&g_TSS, sizeof(TSS) - 1, 0x89, 0x00);

    // Load Task Register (TR)
    i686_LTR(i686_GDT_TSS_SEGMENT);

    log_info(MODULE, "Task State Segment (TSS) initialized at 0x%x (limit %u), TR loaded with selector 0x%x.",
             (uint32_t)&g_TSS, (uint32_t)(sizeof(TSS) - 1), i686_GDT_TSS_SEGMENT);
}

void TSS_SetKernelStack(uint32_t esp0)
{
    g_TSS.esp0 = esp0;
}

uint32_t TSS_GetKernelStack(void)
{
    return g_TSS.esp0;
}
