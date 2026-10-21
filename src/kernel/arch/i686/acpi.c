#include "acpi.h"
#include <hal/vmm.h>
#include <debug.h>
#include <string.h>
#include <memory.h>

#define MODULE "ACPI"

static ACPI_Info g_ACPIInfo;

static void EnsureMapped(uint32_t addr, uint32_t length)
{
    if (addr == 0 || length == 0) return;
    uint32_t start = addr & ~0xFFF;
    uint32_t end = (addr + length + 0xFFF) & ~0xFFF;
    for (uint32_t p = start; p < end; p += 4096)
    {
        if (!VMM_IsMapped((void*)p))
        {
            VMM_MapPage((void*)p, (void*)p, PAGE_PRESENT | PAGE_WRITE);
        }
    }
}

static bool ValidateChecksum(const uint8_t* ptr, size_t length)
{
    uint8_t sum = 0;
    for (size_t i = 0; i < length; i++)
    {
        sum += ptr[i];
    }
    return sum == 0;
}

static const RSDPDescriptor* FindRSDP(void)
{
    // 1. Scan Extended BIOS Data Area (EBDA)
    uint16_t ebda_seg = *(const uint16_t*)0x040E;
    uint32_t ebda_addr = (uint32_t)ebda_seg << 4;
    if (ebda_addr >= 0x80000 && ebda_addr < 0xA0000)
    {
        for (uint32_t addr = ebda_addr; addr < ebda_addr + 1024; addr += 16)
        {
            if (memcmp((const void*)addr, "RSD PTR ", 8) == 0)
            {
                if (ValidateChecksum((const uint8_t*)addr, 20))
                {
                    return (const RSDPDescriptor*)addr;
                }
            }
        }
    }

    // 2. Scan BIOS Read-Only Memory Area (0x000E0000 to 0x000FFFFF)
    for (uint32_t addr = 0x000E0000; addr < 0x00100000; addr += 16)
    {
        if (memcmp((const void*)addr, "RSD PTR ", 8) == 0)
        {
            if (ValidateChecksum((const uint8_t*)addr, 20))
            {
                return (const RSDPDescriptor*)addr;
            }
        }
    }

    return NULL;
}

bool ACPI_Initialize(void)
{
    memset(&g_ACPIInfo, 0, sizeof(ACPI_Info));

    // Default 1:1 legacy IRQ mapping
    for (int i = 0; i < 16; i++)
    {
        g_ACPIInfo.irq_override[i] = (uint8_t)i;
    }

    const RSDPDescriptor* rsdp = FindRSDP();
    if (!rsdp)
    {
        log_warn(MODULE, "ACPI RSDP signature not found in EBDA or BIOS space.");
        return false;
    }

    log_info(MODULE, "Found ACPI RSDP at 0x%x (OEM: %.6s, Rev: %u)",
             (uint32_t)rsdp, rsdp->oem_id, rsdp->revision);

    if (rsdp->rsdt_address == 0)
    {
        log_err(MODULE, "RSDP has NULL RSDT pointer!");
        return false;
    }

    EnsureMapped(rsdp->rsdt_address, sizeof(ACPISDTHeader));
    const RSDT* rsdt = (const RSDT*)rsdp->rsdt_address;
    EnsureMapped(rsdp->rsdt_address, rsdt->header.length);

    if (memcmp(rsdt->header.signature, "RSDT", 4) != 0 ||
        !ValidateChecksum((const uint8_t*)rsdt, rsdt->header.length))
    {
        log_err(MODULE, "RSDT validation failed at 0x%x!", (uint32_t)rsdt);
        return false;
    }

    log_info(MODULE, "Found valid RSDT at 0x%x (length: %u bytes)",
             (uint32_t)rsdt, rsdt->header.length);

    // Locate MADT ('APIC')
    const MADT* madt = NULL;
    int table_count = (rsdt->header.length - sizeof(ACPISDTHeader)) / 4;
    for (int i = 0; i < table_count; i++)
    {
        uint32_t tbl_phys = rsdt->tables[i];
        if (tbl_phys == 0) continue;
        EnsureMapped(tbl_phys, sizeof(ACPISDTHeader));
        const ACPISDTHeader* tbl = (const ACPISDTHeader*)tbl_phys;
        EnsureMapped(tbl_phys, tbl->length);

        if (memcmp(tbl->signature, "APIC", 4) == 0 &&
            ValidateChecksum((const uint8_t*)tbl, tbl->length))
        {
            madt = (const MADT*)tbl;
            break;
        }
    }

    if (!madt)
    {
        log_warn(MODULE, "MADT ('APIC') table not present in RSDT.");
        return false;
    }

    g_ACPIInfo.detected = true;
    g_ACPIInfo.lapic_phys_base = madt->local_apic_address;
    log_info(MODULE, "Found MADT at 0x%x: Default LAPIC MMIO = 0x%x, Dual-PIC Flags = 0x%x",
             (uint32_t)madt, madt->local_apic_address, madt->flags);

    // Parse variable-length MADT records
    const uint8_t* ptr = madt->entries;
    const uint8_t* end = (const uint8_t*)madt + madt->header.length;

    while (ptr < end)
    {
        const MADTRecordHeader* h = (const MADTRecordHeader*)ptr;
        if (h->length == 0) break; // Prevent malformed infinite loops

        if (h->type == 0) // Processor Local APIC
        {
            const MADTRecordLocalAPIC* lapic = (const MADTRecordLocalAPIC*)ptr;
            if ((lapic->flags & 1) && g_ACPIInfo.cpu_count < MAX_CPUS)
            {
                CPU_Core* cpu = &g_ACPIInfo.cpus[g_ACPIInfo.cpu_count];
                cpu->acpi_id  = lapic->acpi_processor_id;
                cpu->apic_id  = lapic->apic_id;
                cpu->enabled  = true;
                cpu->is_bsp   = (g_ACPIInfo.cpu_count == 0); // First core is BSP
                cpu->online   = cpu->is_bsp;

                log_info(MODULE, "  CPU Core %u: ACPI ID %u, APIC ID %u (%s)",
                         g_ACPIInfo.cpu_count, cpu->acpi_id, cpu->apic_id,
                         cpu->is_bsp ? "Bootstrap Processor" : "Application Processor");

                g_ACPIInfo.cpu_count++;
            }
        }
        else if (h->type == 1) // I/O APIC
        {
            const MADTRecordIOAPIC* ioapic = (const MADTRecordIOAPIC*)ptr;
            g_ACPIInfo.ioapic_id = ioapic->ioapic_id;
            g_ACPIInfo.ioapic_phys_base = ioapic->ioapic_address;
            log_info(MODULE, "  I/O APIC ID %u found at MMIO 0x%x (GSI Base: %u)",
                     ioapic->ioapic_id, ioapic->ioapic_address, ioapic->gsi_base);
        }
        else if (h->type == 2) // Interrupt Source Override
        {
            const MADTRecordIntOverride* ov = (const MADTRecordIntOverride*)ptr;
            if (ov->irq_source < 16)
            {
                g_ACPIInfo.irq_override[ov->irq_source] = (uint8_t)ov->gsi;
                log_info(MODULE, "  IRQ Override: ISA IRQ %u -> GSI %u (flags: 0x%x)",
                         ov->irq_source, ov->gsi, ov->flags);
            }
        }

        ptr += h->length;
    }

    log_info(MODULE, "ACPI Initialization Complete: Detected %u active CPU core(s).",
             g_ACPIInfo.cpu_count);

    return true;
}

const ACPI_Info* ACPI_GetInfo(void)
{
    return &g_ACPIInfo;
}

uint32_t ACPI_GetCPUCount(void)
{
    return g_ACPIInfo.cpu_count;
}

bool ACPI_IsDualPICInstalled(void)
{
    return g_ACPIInfo.detected;
}
