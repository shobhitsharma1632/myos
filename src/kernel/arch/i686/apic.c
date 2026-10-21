#include "apic.h"
#include <hal/vmm.h>
#include <debug.h>

#define MODULE "LAPIC"

#define IA32_APIC_BASE_MSR 0x1B
#define IA32_APIC_BASE_MSR_ENABLE 0x800

static volatile uint32_t* g_LAPIC = NULL;

static inline void cpu_get_msr(uint32_t msr, uint32_t* lo, uint32_t* hi)
{
    __asm__ volatile("rdmsr" : "=a"(*lo), "=d"(*hi) : "c"(msr));
}

static inline void cpu_set_msr(uint32_t msr, uint32_t lo, uint32_t hi)
{
    __asm__ volatile("wrmsr" : : "a"(lo), "d"(hi), "c"(msr));
}

uint32_t LAPIC_Read(uint32_t reg)
{
    if (!g_LAPIC) return 0;
    return g_LAPIC[reg / 4];
}

void LAPIC_Write(uint32_t reg, uint32_t val)
{
    if (!g_LAPIC) return;
    g_LAPIC[reg / 4] = val;
}

void LAPIC_EOI(void)
{
    LAPIC_Write(LAPIC_REG_EOI, 0);
}

uint32_t LAPIC_GetId(void)
{
    return (LAPIC_Read(LAPIC_REG_ID) >> 24) & 0xFF;
}

void LAPIC_Enable(void)
{
    // 1. Ensure Hardware APIC is enabled in MSR
    uint32_t lo, hi;
    cpu_get_msr(IA32_APIC_BASE_MSR, &lo, &hi);
    if (!(lo & IA32_APIC_BASE_MSR_ENABLE))
    {
        lo |= IA32_APIC_BASE_MSR_ENABLE;
        cpu_set_msr(IA32_APIC_BASE_MSR, lo, hi);
    }

    // 2. Clear Task Priority Register to accept all interrupts
    LAPIC_Write(LAPIC_REG_TPR, 0);

    // 3. Enable Local APIC in Spurious Interrupt Vector Register (Bit 8 = APIC Software Enable)
    // Map spurious interrupt to vector 0xFF (255)
    LAPIC_Write(LAPIC_REG_SVR, LAPIC_Read(LAPIC_REG_SVR) | 0x100 | 0xFF);
}

bool LAPIC_Initialize(uint32_t lapic_phys_base)
{
    if (lapic_phys_base == 0)
        lapic_phys_base = LAPIC_DEFAULT_PHYS_BASE;

    // Map LAPIC MMIO register page into Kernel Virtual Address Space
    VMM_MapPage((void*)lapic_phys_base, (void*)lapic_phys_base,
                PAGE_PRESENT | PAGE_WRITE | PAGE_CACHE_DISABLE);

    g_LAPIC = (volatile uint32_t*)lapic_phys_base;

    LAPIC_Enable();

    uint32_t id = LAPIC_GetId();
    uint32_t ver = LAPIC_Read(LAPIC_REG_VERSION) & 0xFF;

    log_info(MODULE, "Initialized Local APIC at MMIO 0x%x: Core LAPIC ID %u, Version 0x%x",
             lapic_phys_base, id, ver);

    return true;
}

void LAPIC_SendIPI(uint8_t target_apic_id, uint32_t icr_low)
{
    // Wait for ICR to become ready (not busy)
    while (LAPIC_Read(LAPIC_REG_ICR_LOW) & LAPIC_ICR_BUSY)
    {
        __asm__ volatile("pause");
    }

    // Set destination APIC ID in ICR High (bits 24..31)
    LAPIC_Write(LAPIC_REG_ICR_HIGH, (uint32_t)target_apic_id << 24);

    // Trigger IPI via ICR Low
    LAPIC_Write(LAPIC_REG_ICR_LOW, icr_low);
}

void LAPIC_SendIPIBroadcast(uint8_t vector)
{
    while (LAPIC_Read(LAPIC_REG_ICR_LOW) & LAPIC_ICR_BUSY)
    {
        __asm__ volatile("pause");
    }

    // Destination shorthand: bits 18-19 = 11b (All excluding self) -> (3 << 18) = 0x000C0000
    // Delivery mode: Fixed (0x00000000), Edge triggered
    uint32_t icr_low = (3 << 18) | (uint32_t)vector;
    LAPIC_Write(LAPIC_REG_ICR_LOW, icr_low);
}

void LAPIC_SendInit(uint8_t target_apic_id)
{
    // INIT IPI: delivery mode 5, level assert, edge triggered
    LAPIC_SendIPI(target_apic_id, LAPIC_ICR_INIT | LAPIC_ICR_PHYSICAL | LAPIC_ICR_ASSERT | LAPIC_ICR_EDGE);
}

void LAPIC_SendStartup(uint8_t target_apic_id, uint8_t page_vector)
{
    // Startup IPI (SIPI): delivery mode 6, vector = page_vector
    LAPIC_SendIPI(target_apic_id, LAPIC_ICR_STARTUP | LAPIC_ICR_PHYSICAL | (uint32_t)page_vector);
}
