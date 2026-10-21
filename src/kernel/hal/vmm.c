#include "vmm.h"
#include "pmm.h"
#include "memory.h"
#include "spinlock.h"
#include "smp.h"
#include <debug.h>
#include <arch/i686/isr.h>
#include <arch/i686/io.h>
#include <stdio.h>

static uint32_t* g_KernelPageDirectory = NULL;
static Spinlock  g_VmmLock = SPINLOCK_INIT;

#include <hal/task.h>

void i686_VMM_PageFaultHandler(Registers* regs)
{
    uint32_t faultAddr = i686_VMM_GetFaultAddress();
    uint32_t err = regs->error;

    bool present  = (err & (1 << 0)) != 0;
    bool write    = (err & (1 << 1)) != 0;
    bool user     = (err & (1 << 2)) != 0;
    bool reserved = (err & (1 << 3)) != 0;
    bool ifetch   = (err & (1 << 4)) != 0;

    // Check if the fault originated from Ring 3 User Mode
    if (user || (regs->cs & 3) == 3)
    {
        Task* cur = Task_GetCurrent();
        if (cur && cur->signal_handlers[SIGSEGV] != (uint32_t)SIG_DFL &&
            cur->signal_handlers[SIGSEGV] != (uint32_t)SIG_IGN)
        {
            Task_SendSignal(cur->id, SIGSEGV);
            Task_DeliverSignals(regs);
            return;
        }

        log_warn("VMM", "User Process '%s' (PID %u) triggered Page Fault (#PF) at 0x%x [EIP 0x%x, err 0x%x: %s, %s, %s]!",
                 cur ? cur->name : "unknown",
                 cur ? cur->id : 0,
                 faultAddr, regs->eip, err,
                 present ? "protection violation" : "not present",
                 write ? "write" : "read",
                 ifetch ? "instruction fetch" : "data access");
        printf("\n[USER SEGFAULT] Process '%s' (PID %u) killed: Page Fault at address 0x%x (EIP 0x%x, %s)\n",
               cur ? cur->name : "unknown",
               cur ? cur->id : 0,
               faultAddr, regs->eip,
               present ? "protection violation" : "page not present");

        Task_ExitWithCode(128 + SIGSEGV);
        return;
    }

    log_crit("VMM", "=== KERNEL PAGE FAULT EXCEPTION (#PF) ===");
    log_crit("VMM", "Faulting Address (CR2): 0x%x", faultAddr);
    log_crit("VMM", "Error Code: 0x%x [%s, %s, %s%s%s]",
             err,
             present ? "Protection Violation" : "Page Not Present",
             write ? "Write" : "Read",
             user ? "User Mode (Ring 3)" : "Kernel Mode (Ring 0)",
             reserved ? ", Reserved Bit Set" : "",
             ifetch ? ", Instruction Fetch" : "");
    log_crit("VMM", "EIP: 0x%x | EFLAGS: 0x%x | ESP: 0x%x", regs->eip, regs->eflags, regs->esp);

    printf("\n*** KERNEL PANIC: PAGE FAULT at virtual address 0x%x ***\n", faultAddr);
    printf("    Error 0x%x: %s, %s\n", err,
           present ? "protection violation" : "not present",
           write ? "write" : "read");

    i686_Panic();
}

void VMM_Initialize(const BootParams* bootParams)
{
    log_info("VMM", "Initializing Virtual Memory Manager & Two-Level Paging...");

    // 1. Allocate a 4 KiB physical page frame for the Kernel Page Directory
    g_KernelPageDirectory = (uint32_t*)PMM_AllocatePage();
    if (!g_KernelPageDirectory)
    {
        log_crit("VMM", "Failed to allocate Kernel Page Directory from PMM!");
        return;
    }
    memset(g_KernelPageDirectory, 0, PAGE_SIZE);

    // 2. Identity map all detected physical memory
    // Each page table maps 1024 * 4 KiB = 4 MiB.
    uint32_t totalMemory = PMM_GetTotalMemory();
    uint32_t numTables = (totalMemory + 0x3FFFFF) / 0x400000;
    if (numTables < 4)
        numTables = 4; // Identity map at least 16 MiB
    if (numTables > 1023)
        numTables = 1023; // Reserve entry 1023 for recursive mapping

    log_info("VMM", "Identity mapping %u MB physical RAM (%u Page Tables)...",
             numTables * 4, numTables);

    for (uint32_t t = 0; t < numTables; t++)
    {
        uint32_t* pt = (uint32_t*)PMM_AllocatePage();
        if (!pt)
        {
            log_crit("VMM", "Failed to allocate identity page table %u!", t);
            return;
        }
        memset(pt, 0, PAGE_SIZE);

        for (uint32_t i = 0; i < 1024; i++)
        {
            uint32_t physAddr = (t * 1024 + i) * PAGE_SIZE;
            pt[i] = physAddr | PAGE_PRESENT | PAGE_WRITE;
        }

        g_KernelPageDirectory[t] = ((uint32_t)pt & ~0xFFF) | PAGE_PRESENT | PAGE_WRITE;
    }

    // 3. Set up Recursive Page Directory Mapping (Self-Mapping at PDE 1023)
    // By pointing PDE[1023] back to the Page Directory's own physical frame:
    // - Virtual 0xFFFFF000 maps to the Page Directory
    // - Virtual 0xFFC00000 + (K * 4096) maps to Page Table K
    g_KernelPageDirectory[1023] = ((uint32_t)g_KernelPageDirectory & ~0xFFF) | PAGE_PRESENT | PAGE_WRITE;

    // 4. Register Page Fault (#PF) Handler
    i686_ISR_RegisterHandler(14, i686_VMM_PageFaultHandler);

    // 5. Load CR3 with the Page Directory physical address
    log_info("VMM", "Loading Page Directory physical address 0x%x into CR3...", (uint32_t)g_KernelPageDirectory);
    i686_VMM_LoadPageDirectory((uint32_t)g_KernelPageDirectory);

    // 6. Enable Paging (CR0.PG) and Write-Protect (CR0.WP)
    log_info("VMM", "Activating paging in CR0 (PG=1, WP=1)...");
    i686_VMM_EnablePaging();

    if (i686_VMM_IsPagingEnabled())
    {
        log_info("VMM", "Paging successfully enabled!");
        log_info("VMM", "Recursive self-mapping active at 0xFFFFF000");
    }
    else
    {
        log_crit("VMM", "FATAL: CPU reports paging is not active!");
    }
}

uint32_t* VMM_GetKernelPageDirectory(void)
{
    return g_KernelPageDirectory;
}

static bool vmm_map_page_internal(void* virtAddr, void* physAddr, uint32_t flags)
{
    uint32_t vaddr = (uint32_t)virtAddr;
    uint32_t paddr = (uint32_t)physAddr;

    if ((vaddr & 0xFFF) != 0 || (paddr & 0xFFF) != 0)
    {
        log_err("VMM", "VMM_MapPage: unaligned address virt=0x%x phys=0x%x", vaddr, paddr);
        return false;
    }

    uint32_t pde_index = (vaddr >> 22) & 0x3FF;
    uint32_t pte_index = (vaddr >> 12) & 0x3FF;

    if (pde_index == 1023)
    {
        log_err("VMM", "VMM_MapPage: PDE 1023 is reserved for recursive mapping!");
        return false;
    }

    // Check if the Page Table exists in the Page Directory
    if (!(VMM_PAGE_DIRECTORY_VIRT[pde_index] & PAGE_PRESENT))
    {
        // Allocate a new physical frame for the page table
        void* new_pt_phys = PMM_AllocatePage();
        if (!new_pt_phys)
        {
            log_err("VMM", "VMM_MapPage: failed to allocate page table for virt 0x%x", vaddr);
            return false;
        }

        uint32_t pde_flags = PAGE_PRESENT | PAGE_WRITE;
        if (flags & PAGE_USER)
            pde_flags |= PAGE_USER;

        VMM_PAGE_DIRECTORY_VIRT[pde_index] = ((uint32_t)new_pt_phys & ~0xFFF) | pde_flags;

        // Invalidate TLB for the new page table's virtual mapping window
        i686_VMM_FlushTLB((uint32_t)VMM_PAGE_TABLE_VIRT(pde_index));

        // Zero out the newly created page table
        memset(VMM_PAGE_TABLE_VIRT(pde_index), 0, PAGE_SIZE);
    }
    else
    {
        // Existing page table: elevate PDE flags if user mode or write permission is requested
        if (flags & PAGE_USER)
            VMM_PAGE_DIRECTORY_VIRT[pde_index] |= PAGE_USER;
        if (flags & PAGE_WRITE)
            VMM_PAGE_DIRECTORY_VIRT[pde_index] |= PAGE_WRITE;
    }

    // Set the PTE entry
    uint32_t pte_val = (paddr & ~0xFFF) | (flags & 0xFFF) | PAGE_PRESENT;
    VMM_PAGE_TABLE_VIRT(pde_index)[pte_index] = pte_val;

    // Invalidate the TLB entry for this virtual address locally
    i686_VMM_FlushTLB(vaddr);
    return true;
}

bool VMM_MapPage(void* virtAddr, void* physAddr, uint32_t flags)
{
    uint8_t irq = spinlock_acquire_irqsave(&g_VmmLock);
    bool res = vmm_map_page_internal(virtAddr, physAddr, flags);
    spinlock_release_irqrestore(&g_VmmLock, irq);
    return res;
}

static void vmm_unmap_page_internal(void* virtAddr)
{
    uint32_t vaddr = (uint32_t)virtAddr;
    if (vaddr & 0xFFF)
    {
        log_err("VMM", "VMM_UnmapPage: unaligned address 0x%x", vaddr);
        return;
    }

    uint32_t pde_index = (vaddr >> 22) & 0x3FF;
    uint32_t pte_index = (vaddr >> 12) & 0x3FF;

    if (pde_index == 1023)
        return;

    if (!(VMM_PAGE_DIRECTORY_VIRT[pde_index] & PAGE_PRESENT))
        return;

    VMM_PAGE_TABLE_VIRT(pde_index)[pte_index] = 0;
    i686_VMM_FlushTLB(vaddr);
    SMP_BroadcastTLBShootdown(vaddr);
}

void VMM_UnmapPage(void* virtAddr)
{
    uint8_t irq = spinlock_acquire_irqsave(&g_VmmLock);
    vmm_unmap_page_internal(virtAddr);
    spinlock_release_irqrestore(&g_VmmLock, irq);
}

bool VMM_GetPhysicalAddress(void* virtAddr, void** outPhysAddr)
{
    uint8_t irq = spinlock_acquire_irqsave(&g_VmmLock);
    uint32_t vaddr = (uint32_t)virtAddr;
    uint32_t pde_index = (vaddr >> 22) & 0x3FF;
    uint32_t pte_index = (vaddr >> 12) & 0x3FF;
    uint32_t offset    = vaddr & 0xFFF;

    if (!i686_VMM_IsPagingEnabled())
    {
        if (outPhysAddr)
            *outPhysAddr = virtAddr;
        spinlock_release_irqrestore(&g_VmmLock, irq);
        return true;
    }

    if (!(VMM_PAGE_DIRECTORY_VIRT[pde_index] & PAGE_PRESENT))
    {
        spinlock_release_irqrestore(&g_VmmLock, irq);
        return false;
    }

    uint32_t pte = VMM_PAGE_TABLE_VIRT(pde_index)[pte_index];
    if (!(pte & PAGE_PRESENT))
    {
        spinlock_release_irqrestore(&g_VmmLock, irq);
        return false;
    }

    if (outPhysAddr)
        *outPhysAddr = (void*)((pte & ~0xFFF) | offset);

    spinlock_release_irqrestore(&g_VmmLock, irq);
    return true;
}

bool VMM_IsMapped(void* virtAddr)
{
    void* phys = NULL;
    return VMM_GetPhysicalAddress(virtAddr, &phys);
}
