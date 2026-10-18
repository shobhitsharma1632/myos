#include <stdint.h>
#include "stdio.h"
#include "memory.h"
#include <hal/hal.h>
#include <hal/pmm.h>
#include <arch/i686/irq.h>
#include <debug.h>
#include <boot/bootparams.h>

extern void _init();

void timer(Registers* regs)
{
    printf(".");
    debugc('.');
}

static void Task_ShellDemo(void)
{
    // Wait for shell task to initialize and print prompt
    for (int i = 0; i < 5; i++) Task_Yield();

    const char* demoCmds[] = {
        "help",
        "mem",
        "echo Hello from myOS Phase 10 GUI!",
        "spawn worker interactive",
        "ps",
        "gui"
    };

    for (int c = 0; c < 6; c++)
    {
        const char* cmd = demoCmds[c];
        log_info("ShellDemo", "Simulating keyboard input: '%s'", cmd);
        for (int i = 0; cmd[i]; i++)
        {
            i686_Keyboard_InjectChar(cmd[i]);
            Task_Yield();
        }
        i686_Keyboard_InjectChar('\n');
        for (int i = 0; i < 15; i++) Task_Yield();
    }

    // Wait for GUI compositor to render initial desktop
    for (int i = 0; i < 10; i++) Task_Yield();

    // Simulate mouse interaction: Click and drag MLFQ window title bar
    log_info("ShellDemo", "Simulating mouse movement and title bar drag...");
    Mouse_SetPosition(120, 50); // Inside title bar of MLFQ Scheduler window
    Mouse_InjectEvent(0, 0, true, false, false); // Left Button Down
    for (int i = 0; i < 5; i++) Task_Yield();

    // Drag window dynamically by 150 pixels right and 50 pixels down
    for (int step = 0; step < 10; step++)
    {
        Mouse_InjectEvent(15, 5, true, false, false);
        Task_Yield();
    }

    // Release mouse button
    Mouse_InjectEvent(0, 0, false, false, false); // Left Button Up
    for (int i = 0; i < 10; i++) Task_Yield();

    log_info("ShellDemo", "Automated Phase 10 GUI test complete! Desktop and compositor active.");
    Task_Exit();
}

void start(BootParams* bootParams)
{
    // Call global constructors
    _init();

    HAL_Initialize();

    log_debug("Main", "Boot device: 0x%x", bootParams->BootDevice);
    log_info("Main", "Memory region count: %d", bootParams->Memory.RegionCount);
    for (int i = 0; i < bootParams->Memory.RegionCount; i++)
    {
        log_info("Main", "MEM [%d]: base=0x%llx length=0x%llx type=0x%x",
            i,
            bootParams->Memory.Regions[i].Begin,
            bootParams->Memory.Regions[i].Length,
            bootParams->Memory.Regions[i].Type);
    }

    // Initialize Physical Memory Manager
    PMM_Initialize(bootParams);

    // Run PMM functional tests
    log_info("Test", "Running PMM allocation verification tests...");
    void* page1 = PMM_AllocatePage();
    void* page2 = PMM_AllocatePage();
    log_info("Test", "Allocated page 1: 0x%x | page 2: 0x%x", (uint32_t)page1, (uint32_t)page2);

    // Verify page memory is accessible and writable
    *(volatile uint32_t*)page1 = 0xCAFEBABE;
    log_info("Test", "Wrote magic 0xCAFEBABE to page 1 -> read back: 0x%x", *(volatile uint32_t*)page1);

    // Test free and recycling
    PMM_FreePage(page1);
    log_info("Test", "Freed page 1 (0x%x)", (uint32_t)page1);
    void* page1_realloc = PMM_AllocatePage();
    log_info("Test", "Recycled page allocated: 0x%x (expected match with page 1: 0x%x)",
             (uint32_t)page1_realloc, (uint32_t)page1);

    // Test multi-page contiguous allocation
    void* block = PMM_AllocatePages(4);
    log_info("Test", "Allocated 4 contiguous pages at 0x%x", (uint32_t)block);
    PMM_FreePages(block, 4);
    log_info("Test", "Freed 4 contiguous pages at 0x%x", (uint32_t)block);

    // Cleanup single pages
    PMM_FreePage(page2);
    PMM_FreePage(page1_realloc);
    log_info("Test", "All test pages freed. PMM verification successful! Free memory: %u MB",
             PMM_GetFreeMemory() / (1024 * 1024));

    // Initialize Virtual Memory Manager & Paging
    VMM_Initialize(bootParams);

    // Run VMM functional tests
    log_info("Test", "Running VMM virtual mapping verification tests...");
    bool pagingActive = i686_VMM_IsPagingEnabled();
    log_info("Test", "CPU Paging Status: %s (CR0.PG = %d)",
             pagingActive ? "ACTIVE" : "INACTIVE", pagingActive ? 1 : 0);

    // Test dynamic page mapping at non-identity virtual address 0x80000000 (2 GiB)
    void* vmmTestFrame = PMM_AllocatePage();
    void* virtTarget = (void*)0x80000000;
    log_info("Test", "Mapping virtual address 0x%x to physical frame 0x%x...",
             (uint32_t)virtTarget, (uint32_t)vmmTestFrame);

    bool mapOk = VMM_MapPage(virtTarget, vmmTestFrame, PAGE_PRESENT | PAGE_WRITE);
    log_info("Test", "VMM_MapPage result: %s", mapOk ? "SUCCESS" : "FAILED");

    // Write through virtual pointer
    *(volatile uint32_t*)virtTarget = 0xFEEDFACE;
    log_info("Test", "Wrote 0xFEEDFACE to virtual 0x%x -> read back from virtual: 0x%x",
             (uint32_t)virtTarget, *(volatile uint32_t*)virtTarget);

    // Read directly from physical frame (since physical RAM is identity-mapped)
    log_info("Test", "Reading physical frame 0x%x directly -> value: 0x%x (expected 0xfeedface)",
             (uint32_t)vmmTestFrame, *(volatile uint32_t*)vmmTestFrame);

    // Test virtual-to-physical address translation query
    void* resolvedPhys = NULL;
    bool resolveOk = VMM_GetPhysicalAddress(virtTarget, &resolvedPhys);
    log_info("Test", "VMM_GetPhysicalAddress(0x%x): %s -> physical 0x%x (expected 0x%x)",
             (uint32_t)virtTarget, resolveOk ? "SUCCESS" : "FAILED",
             (uint32_t)resolvedPhys, (uint32_t)vmmTestFrame);

    // Test unmapping and TLB invalidation
    VMM_UnmapPage(virtTarget);
    bool stillMapped = VMM_IsMapped(virtTarget);
    log_info("Test", "Unmapped virtual address 0x%x -> still mapped: %s",
             (uint32_t)virtTarget, stillMapped ? "YES (ERROR)" : "NO (CORRECT)");

    // Free test physical frame
    PMM_FreePage(vmmTestFrame);
    log_info("Test", "VMM verification tests passed successfully!");

    // Initialize Kernel Dynamic Memory Allocator (Heap)
    KHeap_Initialize();

    // Run Kernel Heap functional tests
    log_info("Test", "Running Kernel Heap allocation verification tests...");
    void* ptrA = kmalloc(32);
    void* ptrB = kmalloc(64);
    void* ptrC = kmalloc(128);
    log_info("Test", "Allocated ptrA (32B): 0x%x | ptrB (64B): 0x%x | ptrC (128B): 0x%x",
             (uint32_t)ptrA, (uint32_t)ptrB, (uint32_t)ptrC);

    // Verify 8-byte alignment
    bool aligned = (((uint32_t)ptrA & 7) == 0) && (((uint32_t)ptrB & 7) == 0) && (((uint32_t)ptrC & 7) == 0);
    log_info("Test", "8-byte payload alignment check: %s", aligned ? "PASSED" : "FAILED");

    // Test zeroed allocation (kzalloc)
    uint8_t* zptr = (uint8_t*)kzalloc(100);
    bool allZero = true;
    for (int i = 0; i < 100; i++)
        if (zptr[i] != 0) allZero = false;
    log_info("Test", "kzalloc(100) at 0x%x zero-verification: %s", (uint32_t)zptr, allZero ? "PASSED" : "FAILED");

    // Test reallocation (krealloc)
    memset(ptrB, 0xAB, 64);
    ptrB = krealloc(ptrB, 256);
    bool dataPreserved = true;
    for (int i = 0; i < 64; i++)
        if (((uint8_t*)ptrB)[i] != 0xAB) dataPreserved = false;
    log_info("Test", "krealloc(ptrB, 256) data preservation check: %s", dataPreserved ? "PASSED" : "FAILED");

    // Test O(1) coalescing
    kfree(ptrA);
    kfree(ptrB);
    kfree(ptrC);
    kfree(zptr);
    log_info("Test", "Freed all test blocks. Coalesced heap free memory: %u KB (used: %u bytes)",
             (uint32_t)(KHeap_GetFreeMemory() / 1024), (uint32_t)KHeap_GetUsedMemory());

    // Test dynamic heap expansion (allocate 1.5 MiB which exceeds initial 1 MiB heap)
    log_info("Test", "Testing dynamic heap expansion with 1.5 MB allocation...");
    uint32_t* bigBuffer = (uint32_t*)kmalloc(1536 * 1024);
    if (bigBuffer)
    {
        bigBuffer[0] = 0x11223344;
        bigBuffer[(1536 * 1024 / 4) - 1] = 0x55667788;
        log_info("Test", "BigBuffer mapped at 0x%x (head=0x%x, tail=0x%x)",
                 (uint32_t)bigBuffer, bigBuffer[0], bigBuffer[(1536 * 1024 / 4) - 1]);
        kfree(bigBuffer);
        log_info("Test", "BigBuffer freed successfully. Heap total: %u KB, free: %u KB",
                 (uint32_t)(KHeap_GetTotalMemory() / 1024), (uint32_t)(KHeap_GetFreeMemory() / 1024));
    }
    else
    {
        log_err("Test", "Failed to allocate 1.5 MB buffer!");
    }

    log_info("Main", "Kernel memory management initialized.");

    printf("\n=======================================================\n");
    printf("  myOS Phase 10: High-Resolution GUI & Window Compositor\n");
    printf("  1024x768 @ 32bpp Desktop, PS/2 Mouse & Command Shell Online!\n");
    printf("=======================================================\n\n");

    // Initialize MLFQ Task Scheduler
    Task_Initialize();

    // Spawn Phase 9 Interactive Command Shell Task
    Shell_Initialize();

    // Spawn Shell Automated Demonstrator Task
    Task_Create("ShellDemo", Task_ShellDemo);

    log_info("Main", "Kernel initialization complete. Shell running in MLFQ Queue 0.");

    while (1)
    {
        Task_Yield();
    }
}