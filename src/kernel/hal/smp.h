#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <arch/i686/acpi.h>

#define SMP_TRAMPOLINE_PHYS   0x8000
#define SMP_TRAMPOLINE_VECTOR (SMP_TRAMPOLINE_PHYS >> 12) // 0x08

typedef struct {
    uint32_t cpu_id;
    uint32_t lapic_id;
    bool     online;
    bool     is_bsp;
    uint32_t current_task_id;
    uint32_t ticks;
} SMP_CPU;

bool           SMP_Initialize(void);
uint32_t       SMP_GetCPUCount(void);
uint32_t       SMP_GetOnlineCount(void);
uint32_t       SMP_GetCurrentCPUID(void);
const SMP_CPU* SMP_GetCPU(uint32_t index);
void           SMP_DumpStatus(void);
void           SMP_APEntry(void);
void           SMP_SendRescheduleIPI(uint32_t target_cpu_id);
void           SMP_BroadcastRescheduleIPI(void);
void           SMP_BroadcastTLBShootdown(uint32_t virt_addr);
void           SMP_TLBShootdownHandler(uint32_t virt_addr);
