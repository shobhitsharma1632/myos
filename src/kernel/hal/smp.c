#include "smp.h"
#include <arch/i686/apic.h>
#include <arch/i686/ioapic.h>
#include <arch/i686/idt.h>
#include <arch/i686/io.h>
#include <hal/vmm.h>
#include <hal/heap.h>
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include <memory.h>

#define MODULE "SMP"

extern uint8_t smp_trampoline_start[];
extern uint8_t smp_trampoline_end[];

static SMP_CPU  g_Cpus[MAX_CPUS];
static uint32_t g_TotalCPUs = 1;
static uint32_t g_OnlineCount = 1;

static void SMP_DelayUs(uint32_t us)
{
    for (uint32_t i = 0; i < us; i++)
    {
        i686_iowait();
    }
}

static void SMP_DelayMs(uint32_t ms)
{
    for (uint32_t i = 0; i < ms; i++)
    {
        SMP_DelayUs(1000);
    }
}

void SMP_APEntry(void)
{
    // 1. Initialize IDT for this Application Processor
    i686_IDT_Initialize();

    // 2. Enable this Application Processor's Local APIC
    LAPIC_Enable();

    uint32_t lapic_id = LAPIC_GetId();
    log_info(MODULE, "Hello from AP Core! (LAPIC ID %u online, running on dedicated kernel stack)",
             lapic_id);

    // 3. AP Core execution loop (awaits tasks or halts cleanly)
    while (1)
    {
        __asm__ volatile("sti; hlt");
    }
}

bool SMP_Initialize(void)
{
    memset(g_Cpus, 0, sizeof(g_Cpus));

    // Default configuration: Bootstrap Processor (BSP) is CPU 0
    g_Cpus[0].cpu_id = 0;
    g_Cpus[0].lapic_id = 0;
    g_Cpus[0].online = true;
    g_Cpus[0].is_bsp = true;
    g_TotalCPUs = 1;
    g_OnlineCount = 1;

    // 1. Detect Multi-Core Topology via ACPI MADT
    if (!ACPI_Initialize())
    {
        log_warn(MODULE, "ACPI MADT detection unavailable. Operating in uniprocessor mode (1 CPU).");
        return true;
    }

    const ACPI_Info* info = ACPI_GetInfo();
    g_TotalCPUs = info->cpu_count;
    if (g_TotalCPUs <= 1)
    {
        log_info(MODULE, "Single-core CPU detected. Operating in standard uniprocessor mode.");
        return true;
    }

    log_info(MODULE, "Multi-core CPU detected: %u cores available in hardware.", g_TotalCPUs);

    // 2. Initialize BSP Local APIC & I/O APIC
    if (!LAPIC_Initialize(info->lapic_phys_base))
    {
        log_err(MODULE, "Failed to initialize Local APIC!");
        return false;
    }

    g_Cpus[0].lapic_id = LAPIC_GetId();

    if (info->ioapic_phys_base != 0)
    {
        IOAPIC_Initialize(info->ioapic_phys_base);
    }

    // 3. Prepare Real-Mode Trampoline at 0x8000
    size_t tramp_size = (size_t)(smp_trampoline_end - smp_trampoline_start);
    if (tramp_size > 4096)
    {
        log_crit(MODULE, "SMP Trampoline too large (%u bytes)!", (uint32_t)tramp_size);
        return false;
    }

    memcpy((void*)SMP_TRAMPOLINE_PHYS, smp_trampoline_start, tramp_size);

    // Write GDT descriptor into trampoline offset 0x40
    struct {
        uint16_t limit;
        uint32_t base;
    } __attribute__((packed)) gdt_desc;
    __asm__ volatile("sgdt %0" : "=m"(gdt_desc));
    *(uint16_t*)(SMP_TRAMPOLINE_PHYS + 0x40) = gdt_desc.limit;
    *(uint32_t*)(SMP_TRAMPOLINE_PHYS + 0x42) = gdt_desc.base;

    // Write CR3 (Kernel Page Directory) into trampoline offset 0x48
    uint32_t cr3_val;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3_val));
    *(uint32_t*)(SMP_TRAMPOLINE_PHYS + 0x48) = cr3_val;

    // Write C entry point into trampoline offset 0x50
    *(uint32_t*)(SMP_TRAMPOLINE_PHYS + 0x50) = (uint32_t)SMP_APEntry;

    // 4. Boot Application Processors (APs) using INIT-SIPI-SIPI Sequence
    for (uint32_t i = 1; i < g_TotalCPUs; i++)
    {
        if (!info->cpus[i].enabled)
            continue;

        uint8_t target_apic_id = info->cpus[i].apic_id;
        log_info(MODULE, "Booting AP Core %u (Target LAPIC ID %u)...", i, target_apic_id);

        // Allocate dedicated 16 KiB kernel stack for this AP
        void* ap_stack = kmalloc(16384);
        if (!ap_stack)
        {
            log_err(MODULE, "Failed to allocate kernel stack for AP Core %u!", i);
            continue;
        }

        // Set stack top at trampoline offset 0x4C
        *(uint32_t*)(SMP_TRAMPOLINE_PHYS + 0x4C) = (uint32_t)ap_stack + 16384;

        // Clear online handshake flag at trampoline offset 0x54
        *(volatile uint32_t*)(SMP_TRAMPOLINE_PHYS + 0x54) = 0;

        // Step A: Send INIT IPI
        LAPIC_SendInit(target_apic_id);
        SMP_DelayMs(10); // 10ms delay

        // Step B: Send Startup IPI (SIPI 1) with vector 0x08 (0x8000 >> 12)
        LAPIC_SendStartup(target_apic_id, SMP_TRAMPOLINE_VECTOR);

        // Wait for AP to acknowledge online flag (up to 10ms)
        for (int w = 0; w < 500 && !(*(volatile uint32_t*)(SMP_TRAMPOLINE_PHYS + 0x54)); w++)
        {
            SMP_DelayUs(20);
        }

        // If not yet online, send SIPI 2
        if (!(*(volatile uint32_t*)(SMP_TRAMPOLINE_PHYS + 0x54)))
        {
            LAPIC_SendStartup(target_apic_id, SMP_TRAMPOLINE_VECTOR);
            for (int w = 0; w < 1000 && !(*(volatile uint32_t*)(SMP_TRAMPOLINE_PHYS + 0x54)); w++)
            {
                SMP_DelayUs(20);
            }
        }

        if (*(volatile uint32_t*)(SMP_TRAMPOLINE_PHYS + 0x54) == 1)
        {
            g_Cpus[i].cpu_id   = i;
            g_Cpus[i].lapic_id = target_apic_id;
            g_Cpus[i].online   = true;
            g_Cpus[i].is_bsp   = false;
            g_OnlineCount++;

            log_info(MODULE, "AP Core %u (LAPIC ID %u) BOOTED & ONLINE! (Kernel stack: 0x%x)",
                     i, target_apic_id, (uint32_t)ap_stack);
        }
        else
        {
            log_warn(MODULE, "AP Core %u (LAPIC ID %u) failed to respond to SIPI.",
                     i, target_apic_id);
        }
    }

    log_info(MODULE, "SMP Initialization Complete: %u of %u CPU core(s) active and online.",
             g_OnlineCount, g_TotalCPUs);

    return true;
}

uint32_t SMP_GetCPUCount(void)
{
    return g_TotalCPUs;
}

uint32_t SMP_GetOnlineCount(void)
{
    return g_OnlineCount;
}

uint32_t SMP_GetCurrentCPUID(void)
{
    uint32_t lapic_id = LAPIC_GetId();
    for (uint32_t i = 0; i < g_TotalCPUs; i++)
    {
        if (g_Cpus[i].lapic_id == lapic_id)
            return i;
    }
    return 0;
}

const SMP_CPU* SMP_GetCPU(uint32_t index)
{
    if (index >= MAX_CPUS) return NULL;
    return &g_Cpus[index];
}

void SMP_DumpStatus(void)
{
    printf("\n=== Symmetric Multiprocessing (SMP) Status ===\n");
    printf("  Online Cores : %u / %u\n", g_OnlineCount, g_TotalCPUs);
    printf("  Local APIC   : Base 0x%x (ID: %u)\n", LAPIC_DEFAULT_PHYS_BASE, LAPIC_GetId());
    const ACPI_Info* info = ACPI_GetInfo();
    if (info && info->ioapic_phys_base)
    {
        printf("  I/O APIC     : Base 0x%x (ID: %u)\n",
               info->ioapic_phys_base, info->ioapic_id);
    }
    printf("\n  Core  Role  LAPIC_ID  Status\n");
    printf("  -----------------------------\n");
    for (uint32_t i = 0; i < g_TotalCPUs; i++)
    {
        printf("  CPU %u: Role=%s, LAPIC_ID=%u, Status=%s\n",
               g_Cpus[i].cpu_id,
               g_Cpus[i].is_bsp ? "BSP" : "AP ",
               g_Cpus[i].lapic_id,
               g_Cpus[i].online ? "ONLINE" : "OFFLINE");
    }
    printf("==============================================\n\n");
}
