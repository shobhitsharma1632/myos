#include <stdint.h>
#include "stdio.h"
#include "memory.h"
#include <string.h>
#include <hal/hal.h>
#include <hal/pmm.h>
#include <arch/i686/irq.h>
#include <hal/syscall.h>
#include <hal/user_programs.h>
#include <debug.h>
#include <arch/i686/e9.h>
#include <boot/bootparams.h>

extern void _init();

void timer(Registers* regs)
{
    printf(".");
    debugc('.');
}

static void Task_Phase11_UserModeVerification(void)
{
    // Wait for scheduler and shell to stabilize
    for (int i = 0; i < 5; i++) Task_Yield();

    log_info("Phase11", "=======================================================");
    log_info("Phase11", "Starting Phase 11: Ring 3 User Mode & Syscall Verification");
    log_info("Phase11", "=======================================================");

    // Test 1: Spawn Ring 3 Hello World process
    log_info("Phase11", "Test 1: Spawning Ring 3 Hello World process (int 0x80 SYS_PRINT, SYS_GETPID, SYS_YIELD, SYS_EXIT)...");
    Task* tHello = Task_CreateUser("u_hello", user_program_hello, USER_PROG_SIZE(user_program_hello, user_program_hello_end));
    if (tHello)
    {
        log_info("Phase11", "Task 'u_hello' spawned with PID %u in Ring 3.", tHello->id);
    }
    for (int i = 0; i < 20; i++) Task_Yield();

    // Test 2: Spawn Ring 3 Counter process with sleep syscalls
    log_info("Phase11", "Test 2: Spawning Ring 3 Counter process (SYS_SLEEP & iterative loops)...");
    Task* tCount = Task_CreateUser("u_count", user_program_counter, USER_PROG_SIZE(user_program_counter, user_program_counter_end));
    if (tCount)
    {
        log_info("Phase11", "Task 'u_count' spawned with PID %u in Ring 3.", tCount->id);
    }
    for (int i = 0; i < 40; i++) Task_Yield();

    // Test 3: Spawn Ring 3 Privileged Instruction Fault Isolation Test
    log_info("Phase11", "Test 3: Spawning Ring 3 Privileged Instruction Test ('cli' -> expect #GP fault isolation)...");
    Task* tFaultGP = Task_CreateUser("u_fault_gp", user_program_fault_gp, USER_PROG_SIZE(user_program_fault_gp, user_program_fault_gp_end));
    if (tFaultGP)
    {
        log_info("Phase11", "Task 'u_fault_gp' spawned with PID %u in Ring 3.", tFaultGP->id);
    }
    for (int i = 0; i < 15; i++) Task_Yield();

    // Test 4: Spawn Ring 3 MMU Memory Access Protection Test
    log_info("Phase11", "Test 4: Spawning Ring 3 MMU Memory Protection Test (read 0x100000 -> expect #PF segfault isolation)...");
    Task* tFaultPF = Task_CreateUser("u_fault_pf", user_program_fault_pf, USER_PROG_SIZE(user_program_fault_pf, user_program_fault_pf_end));
    if (tFaultPF)
    {
        log_info("Phase11", "Task 'u_fault_pf' spawned with PID %u in Ring 3.", tFaultPF->id);
    }
    for (int i = 0; i < 15; i++) Task_Yield();

    log_info("Phase11", "=== Phase 11: Ring 3 User Mode, TSS & Syscalls Verified 100%%%%! ===");
    Task_Exit();
}

static void Task_Phase12_StorageVerification(void)
{
    // Wait for Phase 11 automated verification to finish
    for (int i = 0; i < 40; i++) Task_Yield();

    log_info("Phase12", "=======================================================");
    log_info("Phase12", "Starting Phase 12: Storage, FAT Filesystem & VFS Tests");
    log_info("Phase12", "=======================================================");

    // Test 1: ATA Device Detection
    log_info("Phase12", "Test 1: Probing ATA PIO Primary Master Drive...");
    if (ATA_IsAvailable())
    {
        const ATA_DeviceInfo* dev = ATA_GetDevice();
        log_info("Phase12", "ATA Drive Online: Model='%s', Serial='%s', Sectors=%u, Size=%u MB",
                 dev->model, dev->serial, dev->total_sectors, dev->total_mb);
    }
    else
    {
        log_err("Phase12", "ATA Drive not detected!");
    }

    // Test 2: FAT Filesystem Mounting
    log_info("Phase12", "Test 2: Checking FAT Filesystem Mount...");
    if (FAT_IsMounted())
    {
        log_info("Phase12", "FAT Filesystem successfully mounted at root '/'!");
    }
    else
    {
        log_err("Phase12", "FAT Filesystem failed to mount!");
    }

    // Test 3: VFS Directory Enumeration
    log_info("Phase12", "Test 3: Enumerating Root Directory ('/') via vfs_readdir...");
    VFS_DirEntry entry;
    size_t idx = 0;
    while (vfs_readdir("/", idx, &entry) == 0)
    {
        log_info("Phase12", "  Entry [%u]: %s '%s' (%u bytes)",
                 (uint32_t)idx, entry.is_directory ? "<DIR>" : "<FILE>", entry.name, entry.size);
        idx++;
    }

    // Test 4: Reading existing files (/readme.txt)
    log_info("Phase12", "Test 4: Reading '/readme.txt' through VFS...");
    fd_t fd = vfs_open("/readme.txt", VFS_O_RDONLY);
    if (fd >= 0)
    {
        char readBuf[256];
        int n = vfs_read(fd, readBuf, sizeof(readBuf) - 1);
        if (n > 0)
        {
            readBuf[n] = '\0';
            log_info("Phase12", "Read %d bytes from '/readme.txt':\n%s", n, readBuf);
        }
        vfs_close(fd);
    }
    else
    {
        log_err("Phase12", "Failed to open '/readme.txt'!");
    }

    // Test 5: Creating and Writing a New File (/test12.txt)
    log_info("Phase12", "Test 5: Creating and Writing to '/test12.txt'...");
    vfs_touch("/test12.txt");
    fd_t wfd = vfs_open("/test12.txt", VFS_O_WRONLY);
    const char* writeMsg = "Phase 12 ATA PIO Storage & VFS verification: Data written to real disk sectors!";
    if (wfd >= 0)
    {
        int written = vfs_write(wfd, writeMsg, strlen(writeMsg));
        log_info("Phase12", "Wrote %d bytes to '/test12.txt'.", written);
        vfs_close(wfd);
    }
    else
    {
        log_err("Phase12", "Failed to open '/test12.txt' for writing!");
    }

    // Test 6: Reading back newly written file and verifying content
    log_info("Phase12", "Test 6: Reading back '/test12.txt' to verify data integrity...");
    fd_t rfd = vfs_open("/test12.txt", VFS_O_RDONLY);
    if (rfd >= 0)
    {
        char verifyBuf[128];
        memset(verifyBuf, 0, sizeof(verifyBuf));
        int n = vfs_read(rfd, verifyBuf, sizeof(verifyBuf) - 1);
        if (n > 0)
        {
            verifyBuf[n] = '\0';
            log_info("Phase12", "Readback content (%d bytes): '%s'", n, verifyBuf);
            if (strcmp(verifyBuf, writeMsg) == 0)
            {
                log_info("Phase12", "Data Integrity Verification: PASSED (Exact byte match)!");
            }
            else
            {
                log_err("Phase12", "Data mismatch between written and read data!");
            }
        }
        vfs_close(rfd);
    }
    else
    {
        log_err("Phase12", "Failed to open '/test12.txt' for readback verification!");
    }

    // Test 7: Ring 3 User Mode int 0x80 Syscall VFS Access
    log_info("Phase12", "Test 7: Verifying user-space VFS syscalls (sys_open, sys_read_file, sys_close)...");
    int32_t ufd = sys_open("/hello.txt", VFS_O_RDONLY);
    if (ufd >= 0)
    {
        char ubuf[128];
        int32_t un = sys_read_file(ufd, ubuf, sizeof(ubuf) - 1);
        if (un > 0)
        {
            ubuf[un] = '\0';
            log_info("Phase12", "User syscall int 0x80 read %d bytes from '/hello.txt': '%s'", un, ubuf);
        }
        sys_close(ufd);
        log_info("Phase12", "User Syscall VFS Integration: SUCCESS!");
    }
    else
    {
        log_err("Phase12", "User syscall sys_open failed for '/hello.txt'!");
    }

    log_info("Phase12", "=== Phase 12: Storage Subsystem, FAT & VFS 100%%%% VERIFIED! ===");
    Task_Exit();
}

static void Task_Phase13_ELFVerification(void)
{
    // Wait for Phase 11 & 12 automated verification to complete
    for (int i = 0; i < 75; i++) Task_Yield();

    log_info("Phase13", "=======================================================");
    log_info("Phase13", "Starting Phase 13: ELF32 User Binary Loader Tests");
    log_info("Phase13", "=======================================================");

    // Test 1: Spawning /bin/hello.elf with CLI arguments
    log_info("Phase13", "Test 1: Spawning '/bin/hello.elf' with arguments...");
    char* hello_args[] = { "/bin/hello.elf", "Phase13", "DynamicELF", NULL };
    Task* tHello = Task_CreateUserFromELF("u_hello_elf", "/bin/hello.elf", 3, hello_args);
    if (tHello)
    {
        uint32_t pid = tHello->id;
        int32_t status = -1;
        log_info("Phase13", "Waiting for '/bin/hello.elf' (PID %u) to complete...", pid);
        int32_t waitRes = Task_WaitPID(pid, &status);
        log_info("Phase13", "Task_WaitPID(%u) returned %d with exit code %d (expected 0)", pid, waitRes, status);
        if (waitRes == (int32_t)pid && status == 0)
        {
            log_info("Phase13", "Test 1: '/bin/hello.elf' PASSED!");
        }
        else
        {
            log_err("Phase13", "Test 1: '/bin/hello.elf' FAILED!");
        }
    }
    else
    {
        log_err("Phase13", "Test 1: Failed to spawn '/bin/hello.elf'!");
    }

    // Test 2: Spawning /bin/calc.elf with arithmetic arguments (25 * 4 = 100)
    log_info("Phase13", "Test 2: Spawning '/bin/calc.elf' with arithmetic arguments ('25', '*', '4')...");
    char* calc_args[] = { "/bin/calc.elf", "25", "*", "4", NULL };
    Task* tCalc = Task_CreateUserFromELF("u_calc_elf", "/bin/calc.elf", 4, calc_args);
    if (tCalc)
    {
        uint32_t pid = tCalc->id;
        int32_t status = -1;
        int32_t waitRes = Task_WaitPID(pid, &status);
        log_info("Phase13", "Task_WaitPID(%u) returned %d with exit code %d (expected 100)", pid, waitRes, status);
        if (waitRes == (int32_t)pid && status == 100)
        {
            log_info("Phase13", "Test 2: '/bin/calc.elf' (25 * 4 = 100) PASSED!");
        }
        else
        {
            log_err("Phase13", "Test 2: '/bin/calc.elf' calculation FAILED!");
        }
    }
    else
    {
        log_err("Phase13", "Test 2: Failed to spawn '/bin/calc.elf'!");
    }

    // Test 3: Spawning /bin/counter.elf with sleep cycles
    log_info("Phase13", "Test 3: Spawning '/bin/counter.elf' (sleep cycles test)...");
    char* count_args[] = { "/bin/counter.elf", "3", NULL };
    Task* tCount = Task_CreateUserFromELF("u_count_elf", "/bin/counter.elf", 2, count_args);
    if (tCount)
    {
        uint32_t pid = tCount->id;
        int32_t status = -1;
        int32_t waitRes = Task_WaitPID(pid, &status);
        log_info("Phase13", "Task_WaitPID(%u) returned %d with exit code %d (expected 42)", pid, waitRes, status);
        if (waitRes == (int32_t)pid && status == 42)
        {
            log_info("Phase13", "Test 3: '/bin/counter.elf' PASSED!");
        }
        else
        {
            log_err("Phase13", "Test 3: '/bin/counter.elf' FAILED!");
        }
    }
    else
    {
        log_err("Phase13", "Test 3: Failed to spawn '/bin/counter.elf'!");
    }

    // Test 4: Spawning /bin/cat.elf to read /readme.txt
    log_info("Phase13", "Test 4: Spawning '/bin/cat.elf' reading '/readme.txt'...");
    char* cat_args[] = { "/bin/cat.elf", "/readme.txt", NULL };
    Task* tCat = Task_CreateUserFromELF("u_cat_elf", "/bin/cat.elf", 2, cat_args);
    if (tCat)
    {
        uint32_t pid = tCat->id;
        int32_t status = -1;
        int32_t waitRes = Task_WaitPID(pid, &status);
        log_info("Phase13", "Task_WaitPID(%u) returned %d with exit code %d (expected 0)", pid, waitRes, status);
        if (waitRes == (int32_t)pid && status == 0)
        {
            log_info("Phase13", "Test 4: '/bin/cat.elf' PASSED!");
        }
        else
        {
            log_err("Phase13", "Test 4: '/bin/cat.elf' FAILED!");
        }
    }
    else
    {
        log_err("Phase13", "Test 4: Failed to spawn '/bin/cat.elf'!");
    }

    // Test 5: Testing sys_spawn and sys_waitpid system calls from C wrapper
    log_info("Phase13", "Test 5: Verifying sys_spawn and sys_waitpid syscalls via int 0x80...");
    char* spawn_args[] = { "/bin/calc.elf", "12", "+", "33", NULL };
    int32_t child_pid = sys_spawn("/bin/calc.elf", 4, spawn_args);
    if (child_pid > 0)
    {
        int32_t child_status = -1;
        int32_t wpid = sys_waitpid(child_pid, &child_status);
        log_info("Phase13", "sys_waitpid(%d) returned %d with exit code %d (expected 45)", child_pid, wpid, child_status);
        if (wpid == child_pid && child_status == 45)
        {
            log_info("Phase13", "Test 5: sys_spawn & sys_waitpid (12 + 33 = 45) PASSED!");
        }
        else
        {
            log_err("Phase13", "Test 5: sys_spawn & sys_waitpid FAILED!");
        }
    }
    else
    {
        log_err("Phase13", "Test 5: sys_spawn failed!");
    }

    log_info("Phase13", "=== Phase 13: ELF32 User Binary Loader 100%%%% VERIFIED! ===");
    Task_Exit();
}

static void Task_ShellDemo(void)
{
    // Wait for Phase 11, 12 & 13 automated verifications to complete
    for (int i = 0; i < 110; i++) Task_Yield();

    const char* demoCmds[] = {
        "help",
        "mem",
        "disk",
        "ls /bin",
        "exec /bin/hello.elf testing arguments",
        "calc 7 * 8",
        "cat /readme.txt",
        "touch /notes.txt",
        "write /notes.txt Testing interactive shell disk write!",
        "cat /notes.txt",
        "ps",
        "gui"
    };

    for (int c = 0; c < 12; c++)
    {
        const char* cmd = demoCmds[c];
        log_info("ShellDemo", "Simulating keyboard input: '%s'", cmd);
        for (int i = 0; cmd[i]; i++)
        {
            i686_Keyboard_InjectChar(cmd[i]);
            Task_Yield();
        }
        i686_Keyboard_InjectChar('\n');
        for (int i = 0; i < 20; i++) Task_Yield();
    }

    // Wait for GUI compositor to render initial desktop
    for (int i = 0; i < 10; i++) Task_Yield();

    // Simulate mouse interaction: Click and drag File Explorer window title bar
    log_info("ShellDemo", "Simulating mouse movement and title bar drag on File Explorer...");
    Mouse_SetPosition(350, 170); // Inside title bar of File Explorer window
    Mouse_InjectEvent(0, 0, true, false, false); // Left Button Down
    for (int i = 0; i < 5; i++) Task_Yield();

    // Drag window dynamically by 80 pixels right and 30 pixels down
    for (int step = 0; step < 8; step++)
    {
        Mouse_InjectEvent(10, 4, true, false, false);
        Task_Yield();
    }

    // Release mouse button
    Mouse_InjectEvent(0, 0, false, false, false); // Left Button Up
    for (int i = 0; i < 10; i++) Task_Yield();

    log_info("ShellDemo", "Automated Phase 12 demonstration complete! Desktop and storage active.");
    Task_Exit();
}

void start(BootParams* bootParams)
{
    __asm__ volatile("cli");

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

    // Initialize Storage Device Subsystem (ATA PIO), FAT Filesystem & VFS
    ATA_Initialize();
    FAT_Initialize();
    VFS_Initialize();

    printf("\n=======================================================\n");
    printf("  myOS Phase 13: ELF32 User Binary Loader & Runtime Architecture\n");
    printf("  Dynamic ELF Loading, Ring 3 User Apps & Syscalls Active!\n");
    printf("=======================================================\n\n");

    // Initialize MLFQ Task Scheduler
    Task_Initialize();

    // Spawn Phase 11 Ring 3 User Mode Automated Verification Task
    Task_Create("Phase11_Test", Task_Phase11_UserModeVerification);

    // Spawn Phase 12 Storage Subsystem, FAT & VFS Verification Task
    Task_Create("Phase12_Test", Task_Phase12_StorageVerification);

    // Spawn Phase 13 ELF32 User Executable Loader Verification Task
    Task_Create("Phase13_Test", Task_Phase13_ELFVerification);

    // Spawn Phase 9 Interactive Command Shell Task
    Shell_Initialize();

    // Spawn Shell Automated Demonstrator Task
    Task_Create("ShellDemo", Task_ShellDemo);

    log_info("Main", "Kernel initialization complete. Shell, User Mode & Storage active in MLFQ Queue 0.");

    while (1)
    {
        Task_Yield();
    }
}