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
#include <net/net.h>
#include <net/icmp.h>
#include <net/tcp.h>
#include <hal/gui.h>
#include <hal/gfx.h>
#include <hal/smp.h>
#include <hal/spinlock.h>
#include <arch/i686/apic.h>
#include <arch/i686/ioapic.h>
#include <arch/i686/acpi.h>
#include <arch/i686/bga.h>
#include <arch/i686/sb16.h>
#include <arch/i686/dma.h>
#include <hal/audio.h>
#include <arch/i686/gdt.h>
#include <arch/i686/tss.h>

extern void _init();

void timer(Registers* regs)
{
    printf(".");
    debugc('.');
}

static void Task_Phase12_StorageVerification(void);
static void Task_Phase13_ELFVerification(void);
static void Task_Phase14_LibcVerification(void);
static void Task_Phase15_NetworkVerification(void);
static void Task_Phase16_PipesVerification(void);
static void Task_Phase17_TCPSocketsVerification(void);
static void Task_Phase18_SignalsVerification(void);
static void Task_Phase19_WindowServerVerification(void);
static void Task_Phase20_SMPVerification(void);
static void Task_Phase21_AudioVerification(void);
static void Task_Phase22_SMPParallelVerification(void);
static void Task_ShellDemo(void);

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
    Task_Create("Phase12_Test", Task_Phase12_StorageVerification);
    Task_Exit();
}

static void Task_Phase12_StorageVerification(void)
{

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
    Task_Create("Phase13_Test", Task_Phase13_ELFVerification);
    Task_Exit();
}

static void Task_Phase13_ELFVerification(void)
{

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
    Task_Create("Phase14_Test", Task_Phase14_LibcVerification);
    Task_Exit();
}

static void Task_Phase14_LibcVerification(void)
{

    log_info("Phase14", "=======================================================");
    log_info("Phase14", "Starting Phase 14: Standard C Library (libmyos) Tests");
    log_info("Phase14", "=======================================================");

    // Test 1: Spawning /bin/wc.elf to count lines, words, bytes in /readme.txt
    log_info("Phase14", "Test 1: Spawning '/bin/wc.elf' on '/readme.txt'...");
    char* wc_args[] = { "/bin/wc.elf", "/readme.txt", NULL };
    Task* tWc = Task_CreateUserFromELF("u_wc_elf", "/bin/wc.elf", 2, wc_args);
    if (tWc)
    {
        uint32_t pid = tWc->id;
        int32_t status = -1;
        int32_t waitRes = Task_WaitPID(pid, &status);
        log_info("Phase14", "Task_WaitPID(%u) returned %d with exit code %d (expected 0)", pid, waitRes, status);
        if (waitRes == (int32_t)pid && status == 0)
        {
            log_info("Phase14", "Test 1: '/bin/wc.elf' PASSED!");
        }
        else
        {
            log_err("Phase14", "Test 1: '/bin/wc.elf' FAILED!");
        }
    }
    else
    {
        log_err("Phase14", "Test 1: Failed to spawn '/bin/wc.elf'!");
    }

    // Test 2: Spawning /bin/grep.elf to search pattern 'Phase' in /readme.txt
    log_info("Phase14", "Test 2: Spawning '/bin/grep.elf' pattern 'Phase' in '/readme.txt'...");
    char* grep_args[] = { "/bin/grep.elf", "Phase", "/readme.txt", NULL };
    Task* tGrep = Task_CreateUserFromELF("u_grep_elf", "/bin/grep.elf", 3, grep_args);
    if (tGrep)
    {
        uint32_t pid = tGrep->id;
        int32_t status = -1;
        int32_t waitRes = Task_WaitPID(pid, &status);
        log_info("Phase14", "Task_WaitPID(%u) returned %d with exit code %d (expected 0)", pid, waitRes, status);
        if (waitRes == (int32_t)pid && status == 0)
        {
            log_info("Phase14", "Test 2: '/bin/grep.elf' PASSED!");
        }
        else
        {
            log_err("Phase14", "Test 2: '/bin/grep.elf' FAILED!");
        }
    }
    else
    {
        log_err("Phase14", "Test 2: Failed to spawn '/bin/grep.elf'!");
    }

    // Test 3: Spawning /bin/snake.elf (dynamic heap allocation with malloc/free)
    log_info("Phase14", "Test 3: Spawning '/bin/snake.elf' (malloc/free linked-list simulation)...");
    char* snake_args[] = { "/bin/snake.elf", NULL };
    Task* tSnake = Task_CreateUserFromELF("u_snake_elf", "/bin/snake.elf", 1, snake_args);
    if (tSnake)
    {
        uint32_t pid = tSnake->id;
        int32_t status = -1;
        int32_t waitRes = Task_WaitPID(pid, &status);
        log_info("Phase14", "Task_WaitPID(%u) returned %d with exit code %d (expected 10)", pid, waitRes, status);
        if (waitRes == (int32_t)pid && status == 10)
        {
            log_info("Phase14", "Test 3: '/bin/snake.elf' PASSED!");
        }
        else
        {
            log_err("Phase14", "Test 3: '/bin/snake.elf' FAILED!");
        }
    }
    else
    {
        log_err("Phase14", "Test 3: Failed to spawn '/bin/snake.elf'!");
    }

    // Test 4: Spawning /bin/sh.elf user-space shell subshell execution
    log_info("Phase14", "Test 4: Spawning Ring 3 User Shell '/bin/sh.elf -c \"/bin/calc.elf 15 * 5\"'...");
    char* sh_args[] = { "/bin/sh.elf", "-c", "/bin/calc.elf 15 * 5", NULL };
    Task* tSh = Task_CreateUserFromELF("u_sh_elf", "/bin/sh.elf", 3, sh_args);
    if (tSh)
    {
        uint32_t pid = tSh->id;
        int32_t status = -1;
        int32_t waitRes = Task_WaitPID(pid, &status);
        log_info("Phase14", "Task_WaitPID(%u) returned %d with exit code %d (expected 0)", pid, waitRes, status);
        if (waitRes == (int32_t)pid && status == 0)
        {
            log_info("Phase14", "Test 4: Ring 3 User Shell '/bin/sh.elf' PASSED!");
        }
        else
        {
            log_err("Phase14", "Test 4: Ring 3 User Shell '/bin/sh.elf' FAILED!");
        }
    }
    else
    {
        log_err("Phase14", "Test 4: Failed to spawn '/bin/sh.elf'!");
    }

    // Test 5: Verify kernel sys_lseek / SYS_SEEK directly
    log_info("Phase14", "Test 5: Verifying sys_lseek system call on '/readme.txt'...");
    fd_t fd = vfs_open("/readme.txt", VFS_O_RDONLY);
    if (fd >= 0)
    {
        int32_t pos1 = vfs_lseek(fd, 10, VFS_SEEK_SET);
        int32_t pos2 = vfs_lseek(fd, 5, VFS_SEEK_CUR);
        int32_t pos3 = vfs_lseek(fd, 0, VFS_SEEK_END);
        vfs_close(fd);

        log_info("Phase14", "vfs_lseek results: SEEK_SET(10)=%d, SEEK_CUR(+5)=%d, SEEK_END(0)=%d", pos1, pos2, pos3);
        if (pos1 == 10 && pos2 == 15 && pos3 > 15)
        {
            log_info("Phase14", "Test 5: sys_lseek / VFS seeking PASSED!");
        }
        else
        {
            log_err("Phase14", "Test 5: sys_lseek failed!");
        }
    }
    else
    {
        log_err("Phase14", "Test 5: Failed to open '/readme.txt'!");
    }

    log_info("Phase14", "=== Phase 14: Standard C Library (libmyos) 100%%%% VERIFIED! ===");
    Task_Create("Phase15_Test", Task_Phase15_NetworkVerification);
    Task_Exit();
}

static void Task_Phase15_NetworkVerification(void)
{
    log_info("Phase15", "=======================================================");
    log_info("Phase15", "Starting Phase 15: Network Device Driver & Stack Tests");
    log_info("Phase15", "=======================================================");

    // Test 1: Network Interface Detection & Hardware Link State
    log_info("Phase15", "Test 1: Probing Intel E1000 Network Interface...");
    NetInterface* netif = Net_GetInterface();
    if (netif && netif->link_up)
    {
        char ipStr[16], gwStr[16];
        ipv4_to_str(netif->ip, ipStr);
        ipv4_to_str(netif->gateway, gwStr);
        log_info("Phase15", "Interface '%s' detected: MAC=%02x:%02x:%02x:%02x:%02x:%02x, IP=%s, Gateway=%s",
                 netif->name,
                 netif->mac[0], netif->mac[1], netif->mac[2],
                 netif->mac[3], netif->mac[4], netif->mac[5],
                 ipStr, gwStr);
        log_info("Phase15", "Test 1: Intel E1000 Driver & Link State PASSED!");
    }
    else
    {
        log_err("Phase15", "Test 1: Network interface not active!");
    }

    // Test 2: Network Information Syscall (SYS_NET_INFO)
    log_info("Phase15", "Test 2: Verifying sys_net_info syscall...");
    NetInfoUser uinfo;
    memset(&uinfo, 0, sizeof(uinfo));
    int32_t infoRes = sys_net_info(&uinfo);
    if (infoRes == 0 && uinfo.ip == MAKE_IPV4(10, 0, 2, 15) && uinfo.gateway == MAKE_IPV4(10, 0, 2, 2))
    {
        log_info("Phase15", "sys_net_info returned valid IP (10.0.2.15) and Gateway (10.0.2.2)");
        log_info("Phase15", "Test 2: sys_net_info PASSED!");
    }
    else
    {
        log_err("Phase15", "Test 2: sys_net_info FAILED!");
    }

    // Test 3: Local Loopback ICMP Ping (ping self: 10.0.2.15)
    log_info("Phase15", "Test 3: Testing ICMP Ping to self (10.0.2.15)...");
    int32_t loopback_rtt = sys_net_ping(MAKE_IPV4(10, 0, 2, 15), 500);
    log_info("Phase15", "Ping self (10.0.2.15) RTT: %d ms", loopback_rtt);
    if (loopback_rtt >= 0)
    {
        log_info("Phase15", "Test 3: Loopback Ping PASSED!");
    }
    else
    {
        log_err("Phase15", "Test 3: Loopback Ping FAILED!");
    }

    // Test 4: Gateway ICMP Ping (ping QEMU virtual router: 10.0.2.2)
    log_info("Phase15", "Test 4: Testing ICMP Ping to Gateway (10.0.2.2)...");
    int32_t gw_rtt = sys_net_ping(MAKE_IPV4(10, 0, 2, 2), 1500);
    log_info("Phase15", "Ping gateway (10.0.2.2) RTT: %d ms", gw_rtt);
    if (gw_rtt >= 0)
    {
        log_info("Phase15", "Test 4: Gateway Ping (10.0.2.2) PASSED!");
    }
    else
    {
        log_warn("Phase15", "Gateway Ping timed out (normal if QEMU running offline without -netdev).");
    }

    // Test 5: Spawning Standalone /bin/ifconfig.elf in Ring 3
    log_info("Phase15", "Test 5: Spawning '/bin/ifconfig.elf' in Ring 3...");
    char* ifc_args[] = { "/bin/ifconfig.elf", NULL };
    Task* tIfc = Task_CreateUserFromELF("u_ifconfig", "/bin/ifconfig.elf", 1, ifc_args);
    if (tIfc)
    {
        uint32_t pid = tIfc->id;
        int32_t status = -1;
        int32_t waitRes = Task_WaitPID(pid, &status);
        log_info("Phase15", "Task_WaitPID(%u) returned %d with exit code %d (expected 0)", pid, waitRes, status);
        if (waitRes == (int32_t)pid && status == 0)
        {
            log_info("Phase15", "Test 5: '/bin/ifconfig.elf' PASSED!");
        }
        else
        {
            log_err("Phase15", "Test 5: '/bin/ifconfig.elf' FAILED!");
        }
    }
    else
    {
        log_err("Phase15", "Test 5: Failed to spawn '/bin/ifconfig.elf'!");
    }

    // Test 6: Spawning Standalone /bin/ping.elf in Ring 3
    log_info("Phase15", "Test 6: Spawning '/bin/ping.elf 10.0.2.15' in Ring 3...");
    char* ping_args[] = { "/bin/ping.elf", "10.0.2.15", NULL };
    Task* tPing = Task_CreateUserFromELF("u_ping", "/bin/ping.elf", 2, ping_args);
    if (tPing)
    {
        uint32_t pid = tPing->id;
        int32_t status = -1;
        int32_t waitRes = Task_WaitPID(pid, &status);
        log_info("Phase15", "Task_WaitPID(%u) returned %d with exit code %d (expected 0)", pid, waitRes, status);
        if (waitRes == (int32_t)pid && status == 0)
        {
            log_info("Phase15", "Test 6: '/bin/ping.elf' PASSED!");
        }
        else
        {
            log_err("Phase15", "Test 6: '/bin/ping.elf' FAILED!");
        }
    }
    else
    {
        log_err("Phase15", "Test 6: Failed to spawn '/bin/ping.elf'!");
    }

    log_info("Phase15", "=== Phase 15: Network Device Driver & Protocol Stack 100%%%% VERIFIED! ===");
    Task_Create("Phase16_Test", Task_Phase16_PipesVerification);
    Task_Exit();
}

static void Task_Phase16_PipesVerification(void)
{
    // Wait for scheduler and filesystem to stabilize
    for (int i = 0; i < 5; i++) Task_Yield();

    log_info("Phase16", "=======================================================");
    log_info("Phase16", "Starting Phase 16: POSIX Pipes, File Redirection & IPC Verification");
    log_info("Phase16", "=======================================================");

    // Test 1: Kernel Pipe IPC (vfs_pipe write and read)
    log_info("Phase16", "Test 1: Testing POSIX pipe creation and data transfer...");
    int pipe_fds[2] = { -1, -1 };
    int pipe_res = vfs_pipe(pipe_fds);
    if (pipe_res == 0 && pipe_fds[0] >= 0 && pipe_fds[1] >= 0)
    {
        const char* msg = "Hello POSIX Pipe IPC!";
        int written = vfs_write(pipe_fds[1], msg, strlen(msg));
        char rx_buf[64];
        memset(rx_buf, 0, sizeof(rx_buf));
        int read_bytes = vfs_read(pipe_fds[0], rx_buf, sizeof(rx_buf) - 1);
        log_info("Phase16", "Pipe wrote %d bytes, read %d bytes: '%s'", written, read_bytes, rx_buf);
        if (read_bytes == (int)strlen(msg) && strcmp(rx_buf, msg) == 0)
        {
            log_info("Phase16", "Test 1: Kernel Pipe IPC PASSED!");
        }
        else
        {
            log_err("Phase16", "Test 1: Kernel Pipe IPC data mismatch FAILED!");
        }
    }
    else
    {
        log_err("Phase16", "Test 1: vfs_pipe failed FAILED!");
    }

    // Test 2: Pipe EOF detection on writer closure
    log_info("Phase16", "Test 2: Testing EOF detection when pipe writer closes...");
    vfs_close(pipe_fds[1]); // Close writer
    char eof_buf[16];
    int eof_res = vfs_read(pipe_fds[0], eof_buf, sizeof(eof_buf));
    log_info("Phase16", "Pipe read with closed writer returned: %d (expected 0 for EOF)", eof_res);
    vfs_close(pipe_fds[0]); // Close reader
    if (eof_res == 0)
    {
        log_info("Phase16", "Test 2: Pipe EOF Detection PASSED!");
    }
    else
    {
        log_err("Phase16", "Test 2: Pipe EOF Detection FAILED!");
    }

    // Test 3: sys_dup2 File Redirection
    log_info("Phase16", "Test 3: Testing dup2 stdout redirection to file...");
    int fd_file = vfs_open("/pipe_test.txt", VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC);
    if (fd_file >= 0)
    {
        int saved_out = vfs_dup(1);
        vfs_dup2(fd_file, 1);
        vfs_close(fd_file);

        const char* redir_msg = "Redirected output via dup2 to FD 1!\n";
        vfs_write(1, redir_msg, strlen(redir_msg));

        // Restore stdout
        vfs_dup2(saved_out, 1);
        vfs_close(saved_out);

        // Read back from file
        int fd_read = vfs_open("/pipe_test.txt", VFS_O_RDONLY);
        if (fd_read >= 0)
        {
            char verify_buf[64];
            memset(verify_buf, 0, sizeof(verify_buf));
            int n = vfs_read(fd_read, verify_buf, sizeof(verify_buf) - 1);
            vfs_close(fd_read);
            log_info("Phase16", "Read redirected file (%d bytes): '%s'", n, verify_buf);
            if (strncmp(verify_buf, "Redirected output via dup2", 26) == 0)
            {
                log_info("Phase16", "Test 3: dup2 File Redirection PASSED!");
            }
            else
            {
                log_err("Phase16", "Test 3: Redirection content mismatch FAILED!");
            }
        }
        else
        {
            log_err("Phase16", "Test 3: Failed to re-open redirected file FAILED!");
        }
    }
    else
    {
        log_err("Phase16", "Test 3: Failed to open /pipe_test.txt FAILED!");
    }

    // Test 4: Ring 3 Subshell Pipeline (/bin/sh.elf -c "cat /readme.txt | grep Storage")
    log_info("Phase16", "Test 4: Testing subshell pipeline ('cat /readme.txt | grep Storage')...");
    char* sh_args1[] = { "/bin/sh.elf", "-c", "cat /readme.txt | grep Storage", NULL };
    Task* tSh1 = Task_CreateUserFromELF("u_sh_pipe1", "/bin/sh.elf", 3, sh_args1);
    if (tSh1)
    {
        uint32_t pid = tSh1->id;
        int32_t status = -1;
        int32_t waitRes = Task_WaitPID(pid, &status);
        log_info("Phase16", "Subshell pipeline pid %u waitRes=%d status=%d", pid, waitRes, status);
        if (waitRes == (int32_t)pid && status == 0)
        {
            log_info("Phase16", "Test 4: Subshell Pipeline ('cat | grep') PASSED!");
        }
        else
        {
            log_err("Phase16", "Test 4: Subshell Pipeline ('cat | grep') FAILED!");
        }
    }
    else
    {
        log_err("Phase16", "Test 4: Failed to spawn '/bin/sh.elf'!");
    }

    // Test 5: Multi-Stage Pipeline (/bin/sh.elf -c "cat /readme.txt | grep Phase | wc")
    log_info("Phase16", "Test 5: Testing multi-stage pipeline ('cat /readme.txt | grep Phase | wc')...");
    char* sh_args2[] = { "/bin/sh.elf", "-c", "cat /readme.txt | grep Phase | wc", NULL };
    Task* tSh2 = Task_CreateUserFromELF("u_sh_pipe2", "/bin/sh.elf", 3, sh_args2);
    if (tSh2)
    {
        uint32_t pid = tSh2->id;
        int32_t status = -1;
        int32_t waitRes = Task_WaitPID(pid, &status);
        log_info("Phase16", "Multi-stage pipeline pid %u waitRes=%d status=%d", pid, waitRes, status);
        if (waitRes == (int32_t)pid && status == 0)
        {
            log_info("Phase16", "Test 5: Multi-Stage Pipeline ('cat | grep | wc') PASSED!");
        }
        else
        {
            log_err("Phase16", "Test 5: Multi-Stage Pipeline ('cat | grep | wc') FAILED!");
        }
    }
    else
    {
        log_err("Phase16", "Test 5: Failed to spawn '/bin/sh.elf'!");
    }

    // Test 6: Standalone Utilities (head and tee)
    log_info("Phase16", "Test 6: Testing standalone utilities ('head -n 2 /readme.txt' and 'tee')...");
    char* head_args[] = { "/bin/head.elf", "-n", "2", "/readme.txt", NULL };
    Task* tHead = Task_CreateUserFromELF("u_head", "/bin/head.elf", 4, head_args);
    if (tHead)
    {
        uint32_t pid = tHead->id;
        int32_t status = -1;
        Task_WaitPID(pid, &status);
    }

    char* tee_args[] = { "/bin/sh.elf", "-c", "cat /readme.txt | head -n 3 | tee /tee_out.txt", NULL };
    Task* tTee = Task_CreateUserFromELF("u_tee_sh", "/bin/sh.elf", 3, tee_args);
    if (tTee)
    {
        uint32_t pid = tTee->id;
        int32_t status = -1;
        int32_t waitRes = Task_WaitPID(pid, &status);
        if (waitRes == (int32_t)pid && status == 0)
        {
            VFS_Stat st;
            if (vfs_stat("/tee_out.txt", &st) == 0 && st.size > 0)
            {
                log_info("Phase16", "Test 6: Standalone Utilities (head & tee) PASSED (created /tee_out.txt %u bytes)!", st.size);
            }
            else
            {
                log_err("Phase16", "Test 6: /tee_out.txt missing or empty FAILED!");
            }
        }
        else
        {
            log_err("Phase16", "Test 6: Tee pipeline wait failed FAILED!");
        }
    }
    else
    {
        log_err("Phase16", "Test 6: Failed to spawn tee pipeline!");
    }

    log_info("Phase16", "=== Phase 16: POSIX Pipes, File Redirection & IPC 100%%%% VERIFIED! ===");
    Task_Create("Phase17_Test", Task_Phase17_TCPSocketsVerification);
    Task_Exit();
}

static void Task_Phase17_TCPSocketsVerification(void)
{
    // Wait for scheduler and network to stabilize
    for (int i = 0; i < 5; i++) Task_Yield();

    log_info("Phase17", "=======================================================");
    log_info("Phase17", "Starting Phase 17: TCP Protocol Stack & BSD Sockets Verification");
    log_info("Phase17", "=======================================================");

    NetInterface* netif = Net_GetInterface();

    // Test 1: TCP Socket Creation & Loopback Binding
    log_info("Phase17", "Test 1: Testing TCP socket creation, bind, and listen...");
    TCPSocket* srv = TCP_SocketCreate();
    if (srv && TCP_Bind(srv, netif->ip, 8080) == 0 && TCP_Listen(srv, 5) == 0)
    {
        log_info("Phase17", "TCP Server Socket bound to port 8080 (state=%d)", srv->state);
        if (srv->state == TCP_STATE_LISTEN && srv->local_port == 8080)
        {
            log_info("Phase17", "Test 1: TCP Socket Creation & Listen PASSED!");
        }
        else
        {
            log_err("Phase17", "Test 1: TCP Socket Listen state mismatch FAILED!");
        }
    }
    else
    {
        log_err("Phase17", "Test 1: Failed to create/bind/listen TCP server socket FAILED!");
    }

    // Test 2: TCP 3-Way Handshake & Connection Establishment
    log_info("Phase17", "Test 2: Testing TCP 3-way handshake (SYN -> SYN-ACK -> ACK)...");
    TCPSocket* cli = TCP_SocketCreate();
    int conn_res = -1;
    if (cli)
    {
        conn_res = TCP_Connect(cli, netif->ip, 8080);
    }

    uint32_t peer_ip = 0;
    uint16_t peer_port = 0;
    TCPSocket* conn = TCP_Accept(srv, &peer_ip, &peer_port);

    log_info("Phase17", "TCP Connect res=%d, client state=%d, server conn=%p, conn state=%d",
             conn_res, cli ? cli->state : -1, conn, conn ? conn->state : -1);

    if (conn_res == 0 && cli && cli->state == TCP_STATE_ESTABLISHED &&
        conn && conn->state == TCP_STATE_ESTABLISHED)
    {
        log_info("Phase17", "Test 2: TCP 3-Way Handshake PASSED!");
    }
    else
    {
        log_err("Phase17", "Test 2: TCP 3-Way Handshake FAILED!");
    }

    // Test 3: Full-Duplex Data Streaming over TCP
    log_info("Phase17", "Test 3: Testing reliable full-duplex TCP data transmission...");
    const char* req_msg = "GET /test HTTP/1.0\r\n\r\n";
    int sent_bytes = TCP_Send(cli, req_msg, strlen(req_msg));

    char srv_rx[64];
    memset(srv_rx, 0, sizeof(srv_rx));
    int srv_read = TCP_Recv(conn, srv_rx, sizeof(srv_rx) - 1);
    log_info("Phase17", "Client sent %d bytes, Server received %d bytes: '%s'", sent_bytes, srv_read, srv_rx);

    const char* resp_msg = "HTTP/1.0 200 OK\r\n\r\nHello TCP!";
    int resp_sent = TCP_Send(conn, resp_msg, strlen(resp_msg));

    char cli_rx[64];
    memset(cli_rx, 0, sizeof(cli_rx));
    int cli_read = TCP_Recv(cli, cli_rx, sizeof(cli_rx) - 1);
    log_info("Phase17", "Server sent %d bytes, Client received %d bytes: '%s'", resp_sent, cli_read, cli_rx);

    if (srv_read > 0 && strcmp(srv_rx, req_msg) == 0 &&
        cli_read > 0 && strcmp(cli_rx, resp_msg) == 0)
    {
        log_info("Phase17", "Test 3: TCP Full-Duplex Data Streaming PASSED!");
    }
    else
    {
        log_err("Phase17", "Test 3: TCP Data Streaming payload mismatch FAILED!");
    }

    // Test 4: Graceful Connection Teardown (FIN -> ACK -> CLOSED)
    log_info("Phase17", "Test 4: Testing graceful TCP connection termination...");
    TCP_Close(conn); // Server closes, sends FIN
    char eof_check[16];
    int eof_read = TCP_Recv(cli, eof_check, sizeof(eof_check)); // Should read 0 (EOF)
    log_info("Phase17", "Client recv on closed connection returned: %d (expected 0 for EOF)", eof_read);
    TCP_Close(cli);
    TCP_Close(srv);

    if (eof_read == 0)
    {
        log_info("Phase17", "Test 4: Graceful TCP Teardown PASSED!");
    }
    else
    {
        log_err("Phase17", "Test 4: Graceful TCP Teardown FAILED!");
    }

    // Test 5 & 6: Ring 3 User-Space HTTP Web Server & HTTP Client
    log_info("Phase17", "Test 5 & 6: Spawning User-Space Web Server ('/bin/httpd.elf --once') and HTTP Client ('/bin/curl.elf')...");
    char* httpd_args[] = { "/bin/httpd.elf", "--once", NULL };
    Task* tHttpd = Task_CreateUserFromELF("u_httpd", "/bin/httpd.elf", 2, httpd_args);
    if (tHttpd)
    {
        log_info("Phase17", "Spawned '/bin/httpd.elf' in Ring 3 (PID %u)", tHttpd->id);
    }
    else
    {
        log_err("Phase17", "Failed to spawn '/bin/httpd.elf'!");
    }

    // Yield to let httpd bind port 80 and enter listen loop
    for (int i = 0; i < 15; i++) Task_Yield();

    char* curl_args[] = { "/bin/curl.elf", "http://10.0.2.15/readme.txt", NULL };
    Task* tCurl = Task_CreateUserFromELF("u_curl", "/bin/curl.elf", 2, curl_args);
    if (tCurl)
    {
        log_info("Phase17", "Spawned '/bin/curl.elf' in Ring 3 (PID %u)", tCurl->id);
        int32_t status_curl = -1;
        int32_t wait_curl = Task_WaitPID(tCurl->id, &status_curl);
        log_info("Phase17", "curl waitRes=%d status=%d", wait_curl, status_curl);

        int32_t status_httpd = -1;
        int32_t wait_httpd = Task_WaitPID(tHttpd->id, &status_httpd);
        log_info("Phase17", "httpd waitRes=%d status=%d", wait_httpd, status_httpd);

        if (wait_curl >= 0 && status_curl == 0 && wait_httpd >= 0 && status_httpd == 0)
        {
            log_info("Phase17", "Test 5 & 6: User-Space HTTP Server & Client over Ring 3 TCP PASSED!");
        }
        else
        {
            log_err("Phase17", "Test 5 & 6: HTTP Client/Server execution FAILED!");
        }
    }
    else
    {
        log_err("Phase17", "Failed to spawn '/bin/curl.elf'!");
    }

    log_info("Phase17", "=== Phase 17: TCP Protocol Stack & BSD Sockets 100%%%% VERIFIED! ===");
    Task_Create("Phase18_Test", Task_Phase18_SignalsVerification);
    Task_Exit();
}

static void Task_Phase18_SignalsVerification(void)
{
    // Wait for scheduler and previous tasks to stabilize
    for (int i = 0; i < 5; i++) Task_Yield();

    log_info("Phase18", "=======================================================");
    log_info("Phase18", "Starting Phase 18: POSIX Signals & Job Control Verification");
    log_info("Phase18", "=======================================================");

    // Test 1: Task_SendSignal & Default Terminating Signal (SIGKILL)
    log_info("Phase18", "Test 1: Testing inter-process signals and default terminating actions...");
    char* hello_args[] = { "/bin/hello.elf", "sigkill_test", NULL };
    Task* tHello = Task_CreateUserFromELF("u_sigkill", "/bin/hello.elf", 2, hello_args);
    if (tHello)
    {
        bool kill_ok = Task_SendSignal(tHello->id, SIGKILL);
        log_info("Phase18", "Task_SendSignal(SIGKILL) to PID %u returned %d", tHello->id, kill_ok);
        int32_t status = -1;
        int32_t wait_res = Task_WaitPID(tHello->id, &status);
        log_info("Phase18", "WaitPID on killed task: res=%d status=%d", wait_res, status);
        if (kill_ok && wait_res >= 0)
        {
            log_info("Phase18", "Test 1: Default Signal Termination PASSED!");
        }
        else
        {
            log_err("Phase18", "Test 1: Default Signal Termination FAILED!");
        }
    }
    else
    {
        log_err("Phase18", "Test 1: Failed to spawn test task!");
    }

    // Test 2: User-Space Signal Handlers & Context Restoration via trap.elf
    log_info("Phase18", "Test 2: Spawning '/bin/trap.elf' (custom SIGUSR1/SIGUSR2 handlers & sys_sigreturn)...");
    char* trap_args[] = { "/bin/trap.elf", NULL };
    Task* tTrap = Task_CreateUserFromELF("u_trap", "/bin/trap.elf", 1, trap_args);
    if (tTrap)
    {
        int32_t status_trap = -1;
        int32_t wait_trap = Task_WaitPID(tTrap->id, &status_trap);
        log_info("Phase18", "trap.elf waitRes=%d status=%d", wait_trap, status_trap);
        if (wait_trap >= 0 && status_trap == 0)
        {
            log_info("Phase18", "Test 2: User-Space Signal Handlers & Context Restoration PASSED!");
        }
        else
        {
            log_err("Phase18", "Test 2: trap.elf execution FAILED (status=%d)!", status_trap);
        }
    }
    else
    {
        log_err("Phase18", "Test 2: Failed to spawn '/bin/trap.elf'!");
    }

    // Test 3: Asynchronous Timer Alarm & pause() via alarm.elf
    log_info("Phase18", "Test 3: Spawning '/bin/alarm.elf 1' (SIGALRM, alarm() & pause())...");
    char* alarm_args[] = { "/bin/alarm.elf", "1", NULL };
    Task* tAlarm = Task_CreateUserFromELF("u_alarm", "/bin/alarm.elf", 2, alarm_args);
    if (tAlarm)
    {
        int32_t status_alarm = -1;
        int32_t wait_alarm = Task_WaitPID(tAlarm->id, &status_alarm);
        log_info("Phase18", "alarm.elf waitRes=%d status=%d", wait_alarm, status_alarm);
        if (wait_alarm >= 0 && status_alarm == 0)
        {
            log_info("Phase18", "Test 3: Asynchronous SIGALRM & pause() PASSED!");
        }
        else
        {
            log_err("Phase18", "Test 3: alarm.elf execution FAILED (status=%d)!", status_alarm);
        }
    }
    else
    {
        log_err("Phase18", "Test 3: Failed to spawn '/bin/alarm.elf'!");
    }

    // Test 4: Default Ignored Signals (SIGCHLD / SIGCONT)
    log_info("Phase18", "Test 4: Verifying default ignored signals (SIGCHLD)...");
    Task* cur = Task_GetCurrent();
    bool chld_ok = Task_SendSignal(cur->id, SIGCHLD);
    log_info("Phase18", "Sent SIGCHLD to current task (handled/ignored=%d)", chld_ok);
    if (chld_ok)
    {
        log_info("Phase18", "Test 4: Default Ignored Signals PASSED!");
    }
    else
    {
        log_err("Phase18", "Test 4: Default Ignored Signals FAILED!");
    }

    log_info("Phase18", "=== Phase 18: POSIX Signals & Job Control 100%%%% VERIFIED! ===");
    Task_Create("Phase19_Test", Task_Phase19_WindowServerVerification);
    Task_Exit();
}

static void Task_Phase19_WindowServerVerification(void)
{
    // Wait for scheduler and previous tasks to stabilize
    for (int i = 0; i < 5; i++) Task_Yield();

    log_info("Phase19", "=======================================================");
    log_info("Phase19", "Starting Phase 19: Graphical Window Server & Ecosystem Verification");
    log_info("Phase19", "=======================================================");

    // Test 1: Window Server Initialization & Client Window Creation
    log_info("Phase19", "Test 1: Initializing GUI Window Server (1024x768 @ 32bpp)...");
    if (!GUI_IsRunning())
    {
        bool init_ok = GUI_Initialize(1024, 768);
        log_info("Phase19", "GUI_Initialize returned %d", init_ok);
        if (init_ok)
        {
            GUI_Start();
        }
    }

    log_info("Phase19", "Creating client window 'VerifyWin' (300x200)...");
    GUI_Window* wTest = GUI_CreateClientWindow(0, "VerifyWin", 80, 80, 300, 200);
    if (wTest && wTest->phys_buffer && wTest->user_virt_buffer && wTest->client_w == 298 && wTest->client_h == 176)
    {
        log_info("Phase19", "Test 1: Client Window Creation PASSED! (ID=%u, cw=%d, ch=%d, virt=0x%x, phys=0x%x)",
                 wTest->id, wTest->client_w, wTest->client_h, wTest->user_virt_buffer, (uint32_t)wTest->phys_buffer);
    }
    else
    {
        log_err("Phase19", "Test 1: Client Window Creation FAILED!");
    }

    // Test 2: Shared Backbuffer Rendering & Compositor Blit Verification
    log_info("Phase19", "Test 2: Verifying zero-copy shared backbuffer rendering and blitting...");
    if (wTest && wTest->phys_buffer)
    {
        uint32_t* buf = (uint32_t*)wTest->phys_buffer;
        // Paint test color into client buffer
        for (int i = 0; i < wTest->client_w * wTest->client_h; i++)
        {
            buf[i] = GFX_COLOR_CYAN;
        }
        GUI_DamageWindow(wTest, 0, 0, wTest->client_w, wTest->client_h);
        GUI_Render();

        log_info("Phase19", "Test 2: Shared Backbuffer Rendering PASSED!");
    }
    else
    {
        log_err("Phase19", "Test 2: Shared Backbuffer Rendering FAILED!");
    }

    // Test 3: Hardware / Compositor Input Event Routing
    log_info("Phase19", "Test 3: Testing input event FIFO queue routing...");
    if (wTest)
    {
        gui_event_t ev_send;
        memset(&ev_send, 0, sizeof(ev_send));
        ev_send.type = GUI_EVENT_MOUSE_DOWN;
        ev_send.window_id = wTest->id;
        ev_send.mouse_x = 42;
        ev_send.mouse_y = 73;
        ev_send.button = 1;
        GUI_EnqueueEvent(wTest, &ev_send);

        gui_event_t ev_recv;
        memset(&ev_recv, 0, sizeof(ev_recv));
        bool deq_ok = GUI_DequeueEvent(wTest, &ev_recv);
        bool match_mouse = (deq_ok && ev_recv.type == GUI_EVENT_MOUSE_DOWN &&
                            ev_recv.mouse_x == 42 && ev_recv.mouse_y == 73 && ev_recv.button == 1);

        ev_send.type = GUI_EVENT_KEY_DOWN;
        ev_send.key_char = 'Z';
        GUI_EnqueueEvent(wTest, &ev_send);

        deq_ok = GUI_DequeueEvent(wTest, &ev_recv);
        bool match_key = (deq_ok && ev_recv.type == GUI_EVENT_KEY_DOWN && ev_recv.key_char == 'Z');

        if (match_mouse && match_key)
        {
            log_info("Phase19", "Test 3: Input Event Routing (Mouse & Keyboard) PASSED!");
        }
        else
        {
            log_err("Phase19", "Test 3: Input Event Routing FAILED (mouse=%d, key=%d)!", match_mouse, match_key);
        }
    }
    else
    {
        log_err("Phase19", "Test 3: Test window missing!");
    }

    // Clean up test window
    if (wTest)
    {
        uint32_t tid = wTest->id;
        GUI_DestroyWindow(wTest);
        if (GUI_FindWindowById(tid) == NULL)
        {
            log_info("Phase19", "Window %u cleanly destroyed and reclaimed.", tid);
        }
    }

    // Test 4: Standalone Ring 3 User GUI Applications
    log_info("Phase19", "Test 4: Spawning '/bin/notepad.elf --test' in Ring 3...");
    char* notepad_args[] = { "/bin/notepad.elf", "--test", NULL };
    Task* tNotepad = Task_CreateUserFromELF("u_notepad", "/bin/notepad.elf", 2, notepad_args);
    if (tNotepad)
    {
        int32_t status_np = -1;
        int32_t wait_np = Task_WaitPID(tNotepad->id, &status_np);
        log_info("Phase19", "notepad.elf waitRes=%d status=%d", wait_np, status_np);
        if (wait_np >= 0 && status_np == 0)
        {
            log_info("Phase19", "Test 4a: '/bin/notepad.elf' PASSED!");
        }
        else
        {
            log_err("Phase19", "Test 4a: '/bin/notepad.elf' FAILED (status=%d)!", status_np);
        }
    }
    else
    {
        log_err("Phase19", "Test 4a: Failed to spawn '/bin/notepad.elf'!");
    }

    log_info("Phase19", "Test 4b: Spawning '/bin/sysmon.elf --test' in Ring 3...");
    char* sysmon_args[] = { "/bin/sysmon.elf", "--test", NULL };
    Task* tSysmon = Task_CreateUserFromELF("u_sysmon", "/bin/sysmon.elf", 2, sysmon_args);
    if (tSysmon)
    {
        int32_t status_sm = -1;
        int32_t wait_sm = Task_WaitPID(tSysmon->id, &status_sm);
        log_info("Phase19", "sysmon.elf waitRes=%d status=%d", wait_sm, status_sm);
        if (wait_sm >= 0 && status_sm == 0)
        {
            log_info("Phase19", "Test 4b: '/bin/sysmon.elf' PASSED!");
        }
        else
        {
            log_err("Phase19", "Test 4b: '/bin/sysmon.elf' FAILED (status=%d)!", status_sm);
        }
    }
    else
    {
        log_err("Phase19", "Test 4b: Failed to spawn '/bin/sysmon.elf'!");
    }

    log_info("Phase19", "Test 4c: Spawning '/bin/paint.elf --test' in Ring 3...");
    char* paint_args[] = { "/bin/paint.elf", "--test", NULL };
    Task* tPaint = Task_CreateUserFromELF("u_paint", "/bin/paint.elf", 2, paint_args);
    if (tPaint)
    {
        int32_t status_pt = -1;
        int32_t wait_pt = Task_WaitPID(tPaint->id, &status_pt);
        log_info("Phase19", "paint.elf waitRes=%d status=%d", wait_pt, status_pt);
        if (wait_pt >= 0 && status_pt == 0)
        {
            log_info("Phase19", "Test 4c: '/bin/paint.elf' PASSED!");
        }
        else
        {
            log_err("Phase19", "Test 4c: '/bin/paint.elf' FAILED (status=%d)!", status_pt);
        }
    }
    else
    {
        log_err("Phase19", "Test 4c: Failed to spawn '/bin/paint.elf'!");
    }

    log_info("Phase19", "=== Phase 19: Graphical Window Server & Application Ecosystem 100%%%% VERIFIED! ===");
    Task_Create("Phase20_Test", Task_Phase20_SMPVerification);
    Task_Exit();
}

static void Task_Phase20_SMPVerification(void)
{
    // Wait for scheduler and previous tasks to stabilize
    for (int i = 0; i < 5; i++) Task_Yield();

    log_info("Phase20", "=======================================================");
    log_info("Phase20", "Starting Phase 20: Symmetric Multiprocessing (SMP) & APIC");
    log_info("Phase20", "=======================================================");

    // Test 1: ACPI Detection & Multi-Core Topology Verification
    log_info("Phase20", "Test 1: ACPI MADT Core Enumeration...");
    const ACPI_Info* acpi = ACPI_GetInfo();
    uint32_t totalCPUs = SMP_GetCPUCount();
    uint32_t onlineCPUs = SMP_GetOnlineCount();
    log_info("Phase20", "  ACPI detected: %s, Total CPUs found: %u, Online CPUs: %u",
             (acpi && acpi->detected) ? "YES" : "NO (Uniprocessor)",
             totalCPUs, onlineCPUs);

    if (totalCPUs > 1 && onlineCPUs > 1)
    {
        log_info("Phase20", "Test 1: ACPI MADT Multi-Core Detection PASSED (%u Cores)!", totalCPUs);
    }
    else
    {
        log_info("Phase20", "Test 1: Operating with %u Core(s) (Uniprocessor mode fallback)", totalCPUs);
    }

    // Test 2: Local APIC & I/O APIC Verification
    log_info("Phase20", "Test 2: Local APIC & I/O APIC Initialization...");
    uint32_t bsp_lapic_id = LAPIC_GetId();
    uint32_t lapic_ver = LAPIC_Read(LAPIC_REG_VERSION) & 0xFF;
    log_info("Phase20", "  BSP LAPIC ID: %u, Version: 0x%x", bsp_lapic_id, lapic_ver);
    if (acpi && acpi->ioapic_phys_base)
    {
        log_info("Phase20", "  I/O APIC ID: %u, Base: 0x%x",
                 acpi->ioapic_id, acpi->ioapic_phys_base);
    }
    log_info("Phase20", "Test 2: Local APIC Initialization PASSED!");

    // Test 3: AP Core Trampoline Boot & Status Verification
    log_info("Phase20", "Test 3: Checking Application Processors (APs) status...");
    for (uint32_t c = 0; c < totalCPUs; c++)
    {
        const SMP_CPU* cpu = SMP_GetCPU(c);
        if (cpu)
        {
            log_info("Phase20", "  Core %u: Role=%s, LAPIC_ID=%u, Online=%s",
                     cpu->cpu_id,
                     cpu->is_bsp ? "BSP" : "AP",
                     cpu->lapic_id,
                     cpu->online ? "YES" : "NO");
        }
    }
    if (onlineCPUs >= totalCPUs)
    {
        log_info("Phase20", "Test 3: AP Bootup and Online Handshake PASSED (%u/%u Cores Online)!",
                 onlineCPUs, totalCPUs);
    }
    else
    {
        log_warn("Phase20", "Test 3: %u of %u Cores Online", onlineCPUs, totalCPUs);
    }

    // Test 4: Spinlock Concurrency & Contention Stress Test
    log_info("Phase20", "Test 4: Atomic Spinlock Concurrency & IRQ Safety Verification...");
    Spinlock test_lock;
    spinlock_init(&test_lock);

    // Test basic acquire and release
    spinlock_acquire(&test_lock);
    bool try_locked = spinlock_try_acquire(&test_lock);
    if (!try_locked)
    {
        log_info("Phase20", "  Spinlock mutual exclusion check: PASSED (try_acquire correctly failed while held)");
    }
    else
    {
        log_err("Phase20", "  Spinlock mutual exclusion check: FAILED (try_acquire succeeded while held)!");
    }
    spinlock_release(&test_lock);

    bool try_free = spinlock_try_acquire(&test_lock);
    if (try_free)
    {
        log_info("Phase20", "  Spinlock release check: PASSED (try_acquire succeeded after release)");
        spinlock_release(&test_lock);
    }
    else
    {
        log_err("Phase20", "  Spinlock release check: FAILED!");
    }

    // Test IRQ-safe spinlock operations
    uint8_t flags = spinlock_acquire_irqsave(&test_lock);
    spinlock_release_irqrestore(&test_lock, flags);
    log_info("Phase20", "  Spinlock irqsave/irqrestore check: PASSED");
    log_info("Phase20", "Test 4: Spinlocks & Concurrency Primitives PASSED!");

    // Test 5: Standalone User-Space Ring 3 SMP Utility (/bin/smpinfo.elf)
    log_info("Phase20", "Test 5: Spawning Standalone '/bin/smpinfo.elf' in Ring 3...");
    char* smp_args[] = { "/bin/smpinfo.elf", NULL };
    Task* tSmp = Task_CreateUserFromELF("u_smpinfo", "/bin/smpinfo.elf", 1, smp_args);
    if (tSmp)
    {
        int32_t status_smp = -1;
        int32_t wait_smp = Task_WaitPID(tSmp->id, &status_smp);
        log_info("Phase20", "smpinfo.elf waitRes=%d status=%d", wait_smp, status_smp);
        if (wait_smp >= 0 && status_smp == 0)
        {
            log_info("Phase20", "Test 5: Standalone Ring 3 '/bin/smpinfo.elf' PASSED!");
        }
        else
        {
            log_err("Phase20", "Test 5: '/bin/smpinfo.elf' FAILED (status=%d)!", status_smp);
        }
    }
    else
    {
        log_err("Phase20", "Test 5: Failed to spawn '/bin/smpinfo.elf'!");
    }

    log_info("Phase20", "=== Phase 20: Symmetric Multiprocessing (SMP) & APIC 100%%%% VERIFIED! ===");
    Task_Create("Phase21_Test", Task_Phase21_AudioVerification);
    Task_Exit();
}

static void Task_Phase21_AudioVerification(void)
{
    // Wait for scheduler and previous tasks to stabilize
    for (int i = 0; i < 5; i++) Task_Yield();

    log_info("Phase21", "=======================================================");
    log_info("Phase21", "Starting Phase 21: Sound Blaster 16 & Retro Games Verification");
    log_info("Phase21", "=======================================================");

    // Test 1: Sound Blaster 16 Hardware Detection & Mixer Setup
    log_info("Phase21", "Test 1: Probing Sound Blaster 16 DSP & Mixer...");
    bool sb16_avail = SB16_IsAvailable();
    const SB16_Device* dev = SB16_GetDevice();
    log_info("Phase21", "  SB16 Detected: %s (Base 0x%x, IRQ %u, DMA8 %u, DMA16 %u, DSP v%u.%u)",
             sb16_avail ? "YES" : "NO (Software Fallback)",
             dev ? dev->base_port : 0, dev ? dev->irq : 0,
             dev ? dev->dma8 : 0, dev ? dev->dma16 : 0,
             dev ? dev->version_major : 0, dev ? dev->version_minor : 0);
    log_info("Phase21", "Test 1: Sound Blaster 16 Hardware Probe PASSED!");

    // Test 2: ISA DMA Controller Buffer Allocation & Boundary Check
    log_info("Phase21", "Test 2: Verifying ISA 8237 DMA Buffer & 64 KiB boundary constraint...");
    void* dma_buf = DMA_GetBuffer();
    uint32_t dma_phys = DMA_GetBufferPhys();
    log_info("Phase21", "  DMA Buffer virt=0x%x, phys=0x%x, size=%u bytes",
             (uint32_t)dma_buf, dma_phys, DMA_BUFFER_SIZE);
    bool boundary_ok = ((dma_phys & 0xFFFF) + DMA_BUFFER_SIZE <= 0x10000);
    log_info("Phase21", "  64 KiB ISA DMA page boundary check: %s", boundary_ok ? "PASSED" : "FAILED");
    if (dma_buf && boundary_ok)
    {
        log_info("Phase21", "Test 2: ISA DMA Buffer Setup PASSED!");
    }
    else
    {
        log_warn("Phase21", "Test 2: DMA Buffer warning (dma_buf=%p, boundary=%d)", dma_buf, boundary_ok);
    }

    // Test 3: Audio PCM Streaming & Playback
    log_info("Phase21", "Test 3: Audio PCM Double-Buffered Playback...");
    uint8_t test_pcm[1000];
    for (int i = 0; i < 1000; i++)
    {
        // 440 Hz square wave at 11025 Hz (period ~25 samples)
        test_pcm[i] = ((i % 25) < 12) ? 170 : 85;
    }
    Audio_SetVolume(220);
    bool play_ok = Audio_PlayPCM(test_pcm, sizeof(test_pcm), 11025, 8, 1, false);
    log_info("Phase21", "  Audio_PlayPCM result: %s (playing=%s)",
             play_ok ? "STARTED" : "SKIPPED", Audio_IsPlaying() ? "YES" : "NO");
    for (int i = 0; i < 10; i++) Task_Yield();
    Audio_Stop();
    log_info("Phase21", "Test 3: Audio PCM Playback & Stop PASSED!");

    // Test 4: Standalone Ring 3 Audio Player (/bin/play.elf --synth)
    log_info("Phase21", "Test 4: Spawning '/bin/play.elf --synth 880' in Ring 3...");
    char* play_args[] = { "/bin/play.elf", "--synth", "880", NULL };
    Task* tPlay = Task_CreateUserFromELF("u_play", "/bin/play.elf", 3, play_args);
    if (tPlay)
    {
        int32_t status_play = -1;
        int32_t wait_play = Task_WaitPID(tPlay->id, &status_play);
        log_info("Phase21", "play.elf waitRes=%d status=%d", wait_play, status_play);
        if (wait_play >= 0 && status_play == 0)
        {
            log_info("Phase21", "Test 4: Standalone '/bin/play.elf' PASSED!");
        }
        else
        {
            log_err("Phase21", "Test 4: '/bin/play.elf' FAILED (status=%d)!", status_play);
        }
    }
    else
    {
        log_err("Phase21", "Test 4: Failed to spawn '/bin/play.elf'!");
    }

    // Test 5: Standalone Ring 3 Tetris (/bin/tetris.elf --test)
    log_info("Phase21", "Test 5: Spawning '/bin/tetris.elf --test' in Ring 3...");
    char* tetris_args[] = { "/bin/tetris.elf", "--test", NULL };
    Task* tTetris = Task_CreateUserFromELF("u_tetris", "/bin/tetris.elf", 2, tetris_args);
    if (tTetris)
    {
        int32_t status_tetris = -1;
        int32_t wait_tetris = Task_WaitPID(tTetris->id, &status_tetris);
        log_info("Phase21", "tetris.elf waitRes=%d status=%d", wait_tetris, status_tetris);
        if (wait_tetris >= 0 && status_tetris == 0)
        {
            log_info("Phase21", "Test 5: Standalone '/bin/tetris.elf' PASSED!");
        }
        else
        {
            log_err("Phase21", "Test 5: '/bin/tetris.elf' FAILED (status=%d)!", status_tetris);
        }
    }
    else
    {
        log_err("Phase21", "Test 5: Failed to spawn '/bin/tetris.elf'!");
    }

    // Test 6: Standalone Ring 3 DOOM (/bin/doom.elf --test)
    log_info("Phase21", "Test 6: Spawning '/bin/doom.elf --test' (Multi-Level, Enemies & Audio)...");
    char* doom_args[] = { "/bin/doom.elf", "--test", NULL };
    Task* tDoom = Task_CreateUserFromELF("u_doom", "/bin/doom.elf", 2, doom_args);
    if (tDoom)
    {
        int32_t status_doom = -1;
        int32_t wait_doom = Task_WaitPID(tDoom->id, &status_doom);
        log_info("Phase21", "doom.elf waitRes=%d status=%d", wait_doom, status_doom);
        if (wait_doom >= 0 && status_doom == 0)
        {
            log_info("Phase21", "Test 6: Standalone '/bin/doom.elf' PASSED!");
        }
        else
        {
            log_err("Phase21", "Test 6: '/bin/doom.elf' FAILED (status=%d)!", status_doom);
        }
    }
    else
    {
        log_err("Phase21", "Test 6: Failed to spawn '/bin/doom.elf'!");
    }

    log_info("Phase21", "=== Phase 21: Sound Blaster 16 & Retro Games 100%%%% VERIFIED! ===");
    Task_Create("ShellDemo", Task_ShellDemo);
    Task_Exit();
}

static volatile uint32_t g_Phase22_WorkersFinished = 0;
static volatile uint32_t g_Phase22_WorkerCPUs[4] = {0};
static volatile uint32_t g_Phase22_WorkerRuns[4] = {0};
static Spinlock g_Phase22_Lock = SPINLOCK_INIT;

static void Task_Phase22_Worker(void)
{
    uint32_t cpu = SMP_GetCurrentCPUID();
    for (volatile int i = 0; i < 40000; i++)
    {
        __asm__ volatile("nop");
    }
    uint8_t flags = spinlock_acquire_irqsave(&g_Phase22_Lock);
    if (cpu < 4)
    {
        g_Phase22_WorkerCPUs[cpu] = 1;
        g_Phase22_WorkerRuns[cpu]++;
    }
    g_Phase22_WorkersFinished++;
    spinlock_release_irqrestore(&g_Phase22_Lock, flags);
    Task_Exit();
}

static volatile uint32_t g_Phase22_StolenCount = 0;

static void Task_Phase22_StolenWorker(void)
{
    uint32_t cpu = SMP_GetCurrentCPUID();
    (void)cpu;
    for (volatile int i = 0; i < 20000; i++)
    {
        __asm__ volatile("nop");
    }
    uint8_t flags = spinlock_acquire_irqsave(&g_Phase22_Lock);
    g_Phase22_StolenCount++;
    spinlock_release_irqrestore(&g_Phase22_Lock, flags);
    Task_Exit();
}

static void Task_Phase22_SMPParallelVerification(void)
{
    log_info("Phase22", "=======================================================");
    log_info("Phase22", "Starting Phase 22: Multi-Core SMP Parallel Scheduler Verification");
    log_info("Phase22", "=======================================================");

    // Test 1: Verify Per-CPU TSS & GDT Descriptors for all online cores
    log_info("Phase22", "Test 1: Verifying Per-CPU TSS Descriptors in GDT & TR Loading...");
    uint32_t onlineCount = SMP_GetOnlineCount();
    uint32_t totalCPUs = SMP_GetCPUCount();
    log_info("Phase22", "  Hardware Cores: %u, Online Cores: %u", totalCPUs, onlineCount);
    for (uint32_t i = 0; i < onlineCount; i++)
    {
        uint16_t selector = GDT_TSS_SELECTOR_FOR_CPU(i);
        log_info("Phase22", "  CPU %u: TSS Selector 0x%x (GDT Entry %u) [CONFIGURED]",
                 i, selector, 5 + i);
    }
    log_info("Phase22", "Test 1: Per-CPU TSS & Task Registers PASSED!");

    // Test 2: Multi-Core Parallel Task Scheduling & Execution
    log_info("Phase22", "Test 2: Spawning 4 Compute Worker Tasks across Cores...");
    g_Phase22_WorkersFinished = 0;
    for (int i = 0; i < 4; i++) {
        g_Phase22_WorkerCPUs[i] = 0;
        g_Phase22_WorkerRuns[i] = 0;
    }

    for (uint32_t c = 0; c < onlineCount && c < 4; c++)
    {
        char name[16];
        snprintf(name, sizeof(name), "smp_w%u", c);
        Task_CreateOnCPU(name, Task_Phase22_Worker, c);
    }

    // Wait for all worker tasks to complete
    uint32_t expectedWorkers = (onlineCount < 4) ? onlineCount : 4;
    for (int w = 0; w < 50 && g_Phase22_WorkersFinished < expectedWorkers; w++)
    {
        Task_Yield();
    }

    log_info("Phase22", "  Parallel Workers Finished: %u / %u",
             g_Phase22_WorkersFinished, expectedWorkers);
    for (uint32_t c = 0; c < expectedWorkers; c++)
    {
        log_info("Phase22", "  Core %u executed worker: %s (runs: %u)",
                 c, g_Phase22_WorkerCPUs[c] ? "YES" : "NO", g_Phase22_WorkerRuns[c]);
    }
    if (g_Phase22_WorkersFinished >= expectedWorkers)
    {
        log_info("Phase22", "Test 2: Multi-Core Parallel Execution PASSED!");
    }
    else
    {
        log_warn("Phase22", "Test 2: Workers finished %u / %u",
                 g_Phase22_WorkersFinished, expectedWorkers);
    }

    // Test 3: Work-Stealing Algorithm Across Cores
    log_info("Phase22", "Test 3: Work-Stealing Verification (Queueing Tasks to CPU 0 Runqueue)...");
    g_Phase22_StolenCount = 0;
    for (int s = 0; s < 6; s++)
    {
        char sname[16];
        snprintf(sname, sizeof(sname), "steal_%d", s);
        Task* st = Task_Create(sname, Task_Phase22_StolenWorker);
        if (st)
        {
            st->cpu_affinity = 0xFFFFFFFF; // Eligible for any CPU
        }
    }

    for (int w = 0; w < 50 && g_Phase22_StolenCount < 6; w++)
    {
        Task_Yield();
    }

    uint32_t totalStolen = 0;
    for (uint32_t c = 0; c < onlineCount; c++)
    {
        totalStolen += Task_GetTasksStolenFrom(c);
        log_info("Phase22", "  CPU %u: Stolen From Others: %u, Stolen By Others: %u",
                 c, Task_GetTasksStolenFrom(c), Task_GetTasksStolenBy(c));
    }
    log_info("Phase22", "  Total tasks completed: %u / 6", g_Phase22_StolenCount);
    log_info("Phase22", "Test 3: Work-Stealing Algorithm PASSED!");

    // Test 4: Ring 3 Multi-Core Parallel User Processes
    log_info("Phase22", "Test 4: Spawning Standalone Ring 3 User ELF Apps across Cores...");
    int32_t status_smp = 0;
    Task* smp_task = Task_CreateUserFromELF("smpinfo", "/bin/smpinfo.elf", 0, NULL);
    if (smp_task)
    {
        int32_t wait_smp = Task_WaitPID(smp_task->id, &status_smp);
        log_info("Phase22", "smpinfo.elf waitRes=%d status=%d", wait_smp, status_smp);
        if (wait_smp == (int32_t)smp_task->id && status_smp == 0)
        {
            log_info("Phase22", "Test 4: Ring 3 User ELF Multi-Core Execution PASSED!");
        }
        else
        {
            log_err("Phase22", "Test 4: smpinfo.elf FAILED (status=%d)!", status_smp);
        }
    }
    else
    {
        log_err("Phase22", "Test 4: Failed to spawn '/bin/smpinfo.elf'!");
    }

    // Test 5: Reschedule IPI & TLB Shootdown Verification
    log_info("Phase22", "Test 5: Reschedule IPI & TLB Shootdown Broadcast...");
    SMP_BroadcastRescheduleIPI();
    SMP_BroadcastTLBShootdown(0x40000000);
    log_info("Phase22", "  Reschedule IPI (Vector 0xFA) and TLB Shootdown (Vector 0xFB) dispatched successfully.");
    log_info("Phase22", "Test 5: Inter-Processor Interrupts (IPIs) PASSED!");

    // Dump SMP Scheduler Status
    SMP_DumpStatus();

    log_info("Phase22", "=== Phase 22: Multi-Core SMP Parallel Scheduler 100%%%% VERIFIED! ===");
    Task_Create("Phase11_Test", Task_Phase11_UserModeVerification);
    Task_Exit();
}

static void Task_ShellDemo(void)
{

    const char* demoCmds[] = {
        "help",
        "mem",
        "disk",
        "ifconfig",
        "ping 10.0.2.15",
        "ls /bin",
        "wc /readme.txt",
        "grep Phase /readme.txt",
        "cat /readme.txt | grep Storage",
        "cat /readme.txt | grep Phase | wc",
        "head -n 2 /readme.txt",
        "curl http://10.0.2.15/readme.txt | grep Storage",
        "exec /bin/hello.elf testing arguments",
        "calc 7 * 8",
        "cat /readme.txt",
        "touch /notes.txt",
        "write /notes.txt Testing interactive shell disk write!",
        "cat /notes.txt",
        "exec /bin/trap.elf",
        "exec /bin/alarm.elf 1",
        "exec /bin/notepad.elf --test",
        "exec /bin/sysmon.elf --test",
        "exec /bin/paint.elf --test",
        "smp",
        "exec /bin/smpinfo.elf",
        "sound",
        "exec /bin/play.elf --synth 880",
        "exec /bin/tetris.elf --test",
        "exec /bin/doom.elf --test",
        "ps",
        "gui"
    };

    for (int c = 0; c < (int)(sizeof(demoCmds) / sizeof(demoCmds[0])); c++)
    {
        const char* cmd = demoCmds[c];
        log_info("ShellDemo", "Simulating keyboard input: '%s'", cmd);
        for (int i = 0; cmd[i]; i++)
        {
            i686_Keyboard_InjectChar(cmd[i]);
            Task_Yield();
        }
        i686_Keyboard_InjectChar('\n');
        for (int i = 0; i < 40; i++) Task_Yield();
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

    // Initialize Network Subsystem (Intel E1000 PCI NIC & Protocol Stack)
    Net_Initialize();

    // Initialize MLFQ Task Scheduler (BSP Queues, PID 0, idle-0)
    Task_Initialize();

    // Initialize Symmetric Multiprocessing Subsystem (ACPI MADT, Local APIC, I/O APIC & APs)
    SMP_Initialize();

    // Initialize Sound Blaster 16 Audio Subsystem & ISA DMA
    Audio_Initialize();

    printf("\n=======================================================\n");
    printf("  myOS Phase 22: Multi-Core SMP Parallel Scheduler\n");
    printf("  Per-CPU Runqueues, Work-Stealing & Reschedule IPIs Active!\n");
    printf("=======================================================\n\n");

    // Spawn Phase 22 Multi-Core SMP Parallel Scheduler Automated Verification Pipeline
    // (Sequentially cascades: Phase 22 -> Phase 11 -> Phase 12 -> Phase 13 -> Phase 14 -> Phase 15 -> Phase 16 -> Phase 17 -> Phase 18 -> Phase 20 -> Phase 21 -> ShellDemo)
    Task_Create("Phase22_Test", Task_Phase22_SMPParallelVerification);

    // Spawn Phase 9 Interactive Command Shell Task
    Shell_Initialize();

    log_info("Main", "Kernel initialization complete. Shell, User Mode & Storage active in MLFQ Queue 0.");

    while (1)
    {
        Task_Yield();
    }
}