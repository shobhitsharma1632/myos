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
static bool     g_SMPInitialized = false;

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

#include <arch/i686/gdt.h>
#include <arch/i686/tss.h>
#include <hal/task.h>

extern volatile bool g_SMP_SchedulerReady;

void SMP_APEntry(void)
{
    // 1. Initialize IDT for this Application Processor
    i686_IDT_Initialize();

    // 2. Enable this Application Processor's Local APIC
    LAPIC_Enable();

    uint32_t cpu_id = SMP_GetCurrentCPUID();
    uint32_t lapic_id = LAPIC_GetId();
    log_info(MODULE, "Hello from AP Core %u! (LAPIC ID %u online)", cpu_id, lapic_id);

    // 3. Initialize per-CPU TSS and load Task Register (TR)
    TSS_InitializeAP(cpu_id, KERNEL_DATA_SELECTOR);

    // 4. Initialize per-CPU scheduler runqueues
    Task_APInit(cpu_id);

    // 5. Wait for BSP to signal scheduler readiness
    while (!g_SMP_SchedulerReady)
    {
        __asm__ volatile("pause");
    }

    // 6. Enter AP parallel execution loop
    Task_APStart(cpu_id);
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
        g_SMP_SchedulerReady = true;
        return true;
    }

    log_info(MODULE, "Multi-core CPU detected: %u cores available in hardware.", g_TotalCPUs);

    // 2. Initialize BSP Local APIC & I/O APIC
    if (!LAPIC_Initialize(info->lapic_phys_base))
    {
        log_err(MODULE, "Failed to initialize Local APIC!");
        return false;
    }

    g_SMPInitialized = true;
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

        // Pre-populate CPU slot so AP can immediately resolve its CPU ID via SMP_GetCurrentCPUID()
        g_Cpus[i].cpu_id   = i;
        g_Cpus[i].lapic_id = target_apic_id;
        g_Cpus[i].online   = false;
        g_Cpus[i].is_bsp   = false;

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
            g_Cpus[i].online   = true;
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

    g_SMP_SchedulerReady = true;

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
    if (!g_SMPInitialized)
        return 0;

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

#include <hal/spinlock.h>

static volatile uint32_t g_TLBShootdownAddr = 0;
static Spinlock g_TLBLock = SPINLOCK_INIT;

void SMP_SendRescheduleIPI(uint32_t target_cpu_id)
{
    const SMP_CPU* cpu = SMP_GetCPU(target_cpu_id);
    if (cpu && cpu->online)
    {
        LAPIC_SendIPI((uint8_t)cpu->lapic_id, SMP_IPI_RESCHEDULE_VECTOR);
    }
}

void SMP_BroadcastRescheduleIPI(void)
{
    if (g_OnlineCount > 1)
    {
        LAPIC_SendIPIBroadcast(SMP_IPI_RESCHEDULE_VECTOR);
    }
}

void SMP_BroadcastTLBShootdown(uint32_t virt_addr)
{
    if (g_OnlineCount <= 1)
        return;

    uint8_t flags = spinlock_acquire_irqsave(&g_TLBLock);
    g_TLBShootdownAddr = virt_addr;
    LAPIC_SendIPIBroadcast(SMP_IPI_TLB_SHOOTDOWN_VECTOR);
    spinlock_release_irqrestore(&g_TLBLock, flags);
}

void SMP_TLBShootdownHandler(uint32_t virt_addr)
{
    (void)virt_addr;
    uint32_t addr = g_TLBShootdownAddr;
    if (addr != 0)
    {
        i686_VMM_FlushTLB(addr);
    }
}

void SMP_DumpStatus(void)
{
    printf("\n=== Symmetric Multiprocessing (SMP) Multi-Core Scheduler Status ===\n");
    printf("  Online Cores : %u / %u\n", g_OnlineCount, g_TotalCPUs);
    printf("  Local APIC   : Base 0x%x (Current LAPIC ID: %u)\n", LAPIC_DEFAULT_PHYS_BASE, LAPIC_GetId());
    const ACPI_Info* info = ACPI_GetInfo();
    if (info && info->ioapic_phys_base)
    {
        printf("  I/O APIC     : Base 0x%x (ID: %u)\n",
               info->ioapic_phys_base, info->ioapic_id);
    }
    printf("\n  Core  Role  Status   Active Task            RunQ(Q0/1/2)  Runs   Stolen(From/By)\n");
    printf("  ------------------------------------------------------------------------------------\n");
    for (uint32_t i = 0; i < g_TotalCPUs; i++)
    {
        Task* cur = Task_GetCurrentForCPU(i);
        char task_desc[32];
        if (cur)
            snprintf(task_desc, sizeof(task_desc), "%s (PID %u)", cur->name, cur->id);
        else
            snprintf(task_desc, sizeof(task_desc), "none");

        uint32_t q0 = Task_GetQueueCount(i, 0);
        uint32_t q1 = Task_GetQueueCount(i, 1);
        uint32_t q2 = Task_GetQueueCount(i, 2);
        uint32_t runs = Task_GetTotalTasksRun(i);
        uint32_t stolen_from = Task_GetTasksStolenFrom(i);
        uint32_t stolen_by = Task_GetTasksStolenBy(i);

        printf("  CPU %u: [%s] %s | %s | %u/%u/%u | runs=%u | stolen=%u/%u\n",
               g_Cpus[i].cpu_id,
               g_Cpus[i].is_bsp ? "BSP" : "AP ",
               g_Cpus[i].online ? "ONLINE " : "OFFLINE",
               task_desc,
               q0, q1, q2,
               runs,
               stolen_from, stolen_by);
    }
    printf("====================================================================================\n\n");
}
