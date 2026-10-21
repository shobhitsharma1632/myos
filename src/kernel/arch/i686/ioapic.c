#include "ioapic.h"
#include <hal/vmm.h>
#include <debug.h>

#define MODULE "IOAPIC"

static volatile uint32_t* g_IOAPIC_Base = (volatile uint32_t*)IOAPIC_DEFAULT_PHYS_BASE;
static uint32_t g_MaxRedirectionEntries = 24;

uint32_t IOAPIC_Read(uint8_t reg)
{
    g_IOAPIC_Base[0] = reg; // Select register via IOREGSEL
    return g_IOAPIC_Base[4]; // Read from IOWIN (offset 0x10)
}

void IOAPIC_Write(uint8_t reg, uint32_t val)
{
    g_IOAPIC_Base[0] = reg; // Select register via IOREGSEL
    g_IOAPIC_Base[4] = val; // Write to IOWIN (offset 0x10)
}

void IOAPIC_SetEntry(uint8_t index, uint8_t vector, uint8_t dest_apic_id, bool masked)
{
    if (index >= g_MaxRedirectionEntries)
        return;

    uint32_t reg_low = IOAPIC_REG_REDTBL_BASE + (index * 2);
    uint32_t reg_high = reg_low + 1;

    uint32_t val_low = (uint32_t)vector;
    if (masked)
    {
        val_low |= (1 << 16); // Masked bit
    }

    uint32_t val_high = ((uint32_t)dest_apic_id << 24);

    IOAPIC_Write(reg_high, val_high);
    IOAPIC_Write(reg_low, val_low);
}

void IOAPIC_MaskAll(void)
{
    for (uint32_t i = 0; i < g_MaxRedirectionEntries; i++)
    {
        IOAPIC_SetEntry(i, 0, 0, true);
    }
}

bool IOAPIC_Initialize(uint32_t ioapic_phys_base)
{
    if (ioapic_phys_base == 0)
        ioapic_phys_base = IOAPIC_DEFAULT_PHYS_BASE;

    // Map I/O APIC MMIO register page into Kernel Virtual Address Space
    VMM_MapPage((void*)ioapic_phys_base, (void*)ioapic_phys_base,
                PAGE_PRESENT | PAGE_WRITE | PAGE_CACHE_DISABLE);

    g_IOAPIC_Base = (volatile uint32_t*)ioapic_phys_base;

    uint32_t id = (IOAPIC_Read(IOAPIC_REG_ID) >> 24) & 0x0F;
    uint32_t ver_reg = IOAPIC_Read(IOAPIC_REG_VER);
    uint32_t ver = ver_reg & 0xFF;
    g_MaxRedirectionEntries = ((ver_reg >> 16) & 0xFF) + 1;

    log_info(MODULE, "Initialized I/O APIC ID %u at MMIO 0x%x (Ver 0x%x, Max Redir Entries: %u)",
             id, ioapic_phys_base, ver, g_MaxRedirectionEntries);

    return true;
}
