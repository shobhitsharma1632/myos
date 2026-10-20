#include "shell.h"
#include <arch/i686/keyboard.h>
#include <arch/i686/io.h>
#include <arch/i686/vga_text.h>
#include <hal/task.h>
#include <hal/pmm.h>
#include <hal/vmm.h>
#include <hal/heap.h>
#include <hal/gui.h>
#include <hal/user_programs.h>
#include <hal/syscall.h>
#include <arch/i686/bga.h>
#include <arch/i686/ata.h>
#include <hal/fat.h>
#include <hal/vfs.h>
#include <net/net.h>
#include <net/icmp.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>

#define MAX_LINE_LEN    128
#define MAX_ARGS        16

#define shell_print(...) do { printf(__VA_ARGS__); debugf(__VA_ARGS__); } while(0)
#define shell_putc(c)    do { putc(c); debugc(c); } while(0)
#define shell_puts(s)    do { puts(s); debugs(s); } while(0)

// String helper utilities
static size_t str_len(const char* s)
{
    size_t len = 0;
    while (s && s[len]) len++;
    return len;
}

static int str_cmp(const char* s1, const char* s2)
{
    while (*s1 && (*s1 == *s2))
    {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static int str_ncmp(const char* s1, const char* s2, size_t n)
{
    while (n && *s1 && (*s1 == *s2))
    {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static int str_to_int(const char* s)
{
    int result = 0;
    int sign = 1;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') { s++; }
    while (*s >= '0' && *s <= '9')
    {
        result = result * 10 + (*s - '0');
        s++;
    }
    return result * sign;
}

// Background worker tasks launched via 'spawn' command
static void Shell_InteractiveWorker(void)
{
    Task* cur = Task_GetCurrent();
    shell_print("\n[Task %s (PID %u)] Interactive worker active. Yielding across 10 iterations...\n",
                cur->name, cur->id);
    for (int i = 1; i <= 10; i++)
    {
        for (volatile int j = 0; j < 60000; j++);
        shell_print("[Task %s] Iteration %d/10 complete (yielding CPU)\n", cur->name, i);
        Task_Yield();
    }
    shell_print("[Task %s] All iterations complete. Calling Task_Exit().\n", cur->name);
    Task_Exit();
}

static void Shell_ComputeWorker(void)
{
    Task* cur = Task_GetCurrent();
    shell_print("\n[Task %s (PID %u)] Compute-heavy worker started. Burning quantum (expect demotion Q0->Q1->Q2)...\n",
                cur->name, cur->id);
    while (true)
    {
        for (volatile int j = 0; j < 300000; j++);
    }
}

static void Shell_ExecuteELF(const char* filepath, int argc, char* const argv[])
{
    shell_print("Launching '%s' in Ring 3 user mode...\n", filepath);
    Task* child = Task_CreateUserFromELF(filepath, filepath, argc, argv);
    if (!child)
    {
        shell_print("Error: Failed to load and execute '%s'!\n", filepath);
        return;
    }

    uint32_t pid = child->id;
    int32_t exit_code = 0;
    int32_t waitRes = Task_WaitPID(pid, &exit_code);
    if (waitRes >= 0)
    {
        shell_print("[Process %u terminated with exit code %d]\n", pid, exit_code);
    }
}

static size_t Shell_ReadLine(char* buffer, size_t maxLen)
{
    size_t len = 0;
    while (true)
    {
        char c = i686_Keyboard_WaitChar();

        if (c == '\n' || c == '\r')
        {
            shell_putc('\n');
            buffer[len] = '\0';
            return len;
        }
        else if (c == '\b')
        {
            if (len > 0)
            {
                len--;
                shell_puts("\b \b");
            }
        }
        else if (c == 0x0C) // Ctrl+L (Clear screen)
        {
            clrscr();
            shell_print("myos> ");
            for (size_t i = 0; i < len; i++)
                shell_putc(buffer[i]);
        }
        else if (c == 0x03) // Ctrl+C (Abort line)
        {
            shell_puts("^C\n");
            buffer[0] = '\0';
            return 0;
        }
        else if (c >= 32 && c <= 126)
        {
            if (len < maxLen - 1)
            {
                buffer[len++] = c;
                shell_putc(c);
            }
        }
    }
}

void Shell_Execute(char* cmdLine)
{
    // Skip leading spaces
    while (*cmdLine == ' ' || *cmdLine == '\t') cmdLine++;
    if (*cmdLine == '\0') return;

    // Split command line into tokens
    char* argv[MAX_ARGS];
    int argc = 0;
    char* p = cmdLine;

    while (*p && argc < MAX_ARGS)
    {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;

        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p)
        {
            *p = '\0';
            p++;
        }
    }

    if (argc == 0) return;

    const char* cmd = argv[0];

    // Built-in commands
    if (str_cmp(cmd, "help") == 0)
    {
        shell_print("\nmyOS Built-in Shell Commands:\n");
        shell_print("  help                - Display this list of commands\n");
        shell_print("  clear               - Clear the terminal screen\n");
        shell_print("  ps                  - List active tasks in the MLFQ scheduler\n");
        shell_print("  mem                 - Display PMM, VMM, and Kernel Heap status\n");
        shell_print("  spawn <name> [type] - Launch worker ('interactive' or 'compute')\n");
        shell_print("  user <subcmd>       - Ring 3 user mode commands (hello, count, test-gp, test-pf, test-all)\n");
        shell_print("  disk                - Show ATA drive and FAT filesystem status\n");
        shell_print("  ifconfig            - Display network interface and IP configuration\n");
        shell_print("  ping <ip>           - Send ICMP echo requests to network host\n");
        shell_print("  ls [path]           - List files and directories on disk\n");
        shell_print("  cat <file>          - Read and display file contents\n");
        shell_print("  touch <file>        - Create a new empty file\n");
        shell_print("  write <file> <text> - Write or append text to a file\n");
        shell_print("  exec <elf> [args..] - Execute standalone ELF binary from disk in Ring 3\n");
        shell_print("  kill <pid>          - Terminate task by Process ID\n");
        shell_print("  stats               - Dump live MLFQ scheduling metrics\n");
        shell_print("  gui                 - Launch high-resolution 1024x768 GUI desktop\n");
        shell_print("  echo <text...>      - Print text arguments to screen\n");
        shell_print("  reboot              - Reboot the computer\n\n");
    }
    else if (str_cmp(cmd, "clear") == 0)
    {
        clrscr();
    }
    else if (str_cmp(cmd, "ps") == 0 || str_cmp(cmd, "stats") == 0)
    {
        Task_DumpStats();
    }
    else if (str_cmp(cmd, "mem") == 0)
    {
        shell_print("\n=== System Memory Diagnostics ===\n");
        shell_print("  [Physical Memory Manager - PMM]\n");
        shell_print("    Total Physical RAM : %u MB (%u frames)\n",
                    PMM_GetTotalMemory() / (1024 * 1024), PMM_GetTotalMemory() / 4096);
        shell_print("    Free Physical RAM  : %u MB (%u frames)\n",
                    PMM_GetFreeMemory() / (1024 * 1024), PMM_GetFreeMemory() / 4096);
        shell_print("    Used Physical RAM  : %u KB (%u frames)\n",
                    PMM_GetUsedMemory() / 1024, PMM_GetUsedMemory() / 4096);
        shell_print("\n  [Virtual Memory Manager - VMM]\n");
        shell_print("    Paging Mode        : Two-Level x86 (CR0.PG = %d)\n",
                    i686_VMM_IsPagingEnabled() ? 1 : 0);
        shell_print("    Identity Mapped    : Lower 128 MB (32 Page Tables)\n");
        shell_print("    Recursive PDE 1023 : 0xFFFFF000\n");
        shell_print("\n  [Kernel Dynamic Heap Allocator]\n");
        shell_print("    Heap Base Virtual  : 0x10000000 (256 MB boundary)\n");
        shell_print("    Heap Total Mapped  : %u KB\n",
                    (uint32_t)(KHeap_GetTotalMemory() / 1024));
        shell_print("    Heap Free Space    : %u KB\n",
                    (uint32_t)(KHeap_GetFreeMemory() / 1024));
        shell_print("    Heap Used Space    : %u bytes\n",
                    (uint32_t)KHeap_GetUsedMemory());
        shell_print("=================================\n\n");
    }
    else if (str_cmp(cmd, "spawn") == 0)
    {
        if (argc < 2)
        {
            shell_print("Usage: spawn <task_name> [interactive|compute]\n");
            return;
        }

        const char* name = argv[1];
        bool isCompute = (argc >= 3 && str_cmp(argv[2], "compute") == 0);

        Task* t = NULL;
        if (isCompute)
            t = Task_Create(name, Shell_ComputeWorker);
        else
            t = Task_Create(name, Shell_InteractiveWorker);

        if (t)
            shell_print("Spawned task '%s' (PID %u) as %s.\n",
                        name, t->id, isCompute ? "COMPUTE HOG" : "INTERACTIVE");
        else
            shell_print("Failed to spawn task '%s'!\n", name);
    }
    else if (str_cmp(cmd, "kill") == 0)
    {
        if (argc < 2)
        {
            shell_print("Usage: kill <pid>\n");
            return;
        }

        uint32_t pid = (uint32_t)str_to_int(argv[1]);
        if (Task_Kill(pid))
            shell_print("Task PID %u successfully terminated.\n", pid);
        else
            shell_print("Failed to terminate task PID %u (PID not found or system protected).\n", pid);
    }
    else if (str_cmp(cmd, "echo") == 0)
    {
        for (int i = 1; i < argc; i++)
        {
            shell_print("%s%s", argv[i], (i == argc - 1) ? "" : " ");
        }
        shell_print("\n");
    }
    else if (str_cmp(cmd, "gui") == 0)
    {
        if (!BGA_IsAvailable())
        {
            shell_print("Error: Bochs Graphics Adapter (BGA) not detected on this hardware.\n");
        }
        else if (GUI_IsRunning())
        {
            shell_print("GUI desktop is already active!\n");
        }
        else
        {
            shell_print("Initializing Bochs VBE 1024x768 @ 32bpp graphics desktop...\n");
            if (GUI_Initialize(1024, 768))
            {
                GUI_Start();
                shell_print("GUI Compositor launched successfully in MLFQ Queue 0.\n");
            }
            else
            {
                shell_print("Failed to initialize GUI graphics mode!\n");
            }
        }
    }
    else if (str_cmp(cmd, "user") == 0)
    {
        if (argc < 2)
        {
            shell_print("Usage: user <hello|count|test-gp|test-pf|test-all>\n");
            shell_print("  hello    - Run Ring 3 Hello World process with int 0x80 syscalls\n");
            shell_print("  count    - Run Ring 3 counter process testing user stack & sleep\n");
            shell_print("  test-gp  - Test Ring 3 fault isolation (privileged 'cli' triggers #GP)\n");
            shell_print("  test-pf  - Test Ring 3 MMU memory protection (kernel read triggers #PF)\n");
            shell_print("  test-all - Run all Ring 3 verification tests in sequence\n");
            return;
        }

        const char* sub = argv[1];
        if (str_cmp(sub, "hello") == 0)
        {
            shell_print("Spawning Ring 3 User Process 'u_hello'...\n");
            Task* t = Task_CreateUser("u_hello", user_program_hello, USER_PROG_SIZE(user_program_hello, user_program_hello_end));
            if (t) shell_print("Spawned user process PID %u in Ring 3!\n", t->id);
            else shell_print("Failed to spawn user process!\n");
        }
        else if (str_cmp(sub, "count") == 0)
        {
            shell_print("Spawning Ring 3 User Process 'u_count'...\n");
            Task* t = Task_CreateUser("u_count", user_program_counter, USER_PROG_SIZE(user_program_counter, user_program_counter_end));
            if (t) shell_print("Spawned user process PID %u in Ring 3!\n", t->id);
            else shell_print("Failed to spawn user process!\n");
        }
        else if (str_cmp(sub, "test-gp") == 0)
        {
            shell_print("Spawning Ring 3 Fault Isolation Test 'u_fault_gp'...\n");
            Task* t = Task_CreateUser("u_fault_gp", user_program_fault_gp, USER_PROG_SIZE(user_program_fault_gp, user_program_fault_gp_end));
            if (t) shell_print("Spawned user process PID %u (expect #GP fault without kernel crash)!\n", t->id);
            else shell_print("Failed to spawn user process!\n");
        }
        else if (str_cmp(sub, "test-pf") == 0)
        {
            shell_print("Spawning Ring 3 MMU Protection Test 'u_fault_pf'...\n");
            Task* t = Task_CreateUser("u_fault_pf", user_program_fault_pf, USER_PROG_SIZE(user_program_fault_pf, user_program_fault_pf_end));
            if (t) shell_print("Spawned user process PID %u (expect #PF fault without kernel crash)!\n", t->id);
            else shell_print("Failed to spawn user process!\n");
        }
        else if (str_cmp(sub, "test-all") == 0)
        {
            shell_print("Launching Ring 3 Verification Suite (hello, count, test-gp, test-pf)...\n");
            Task_CreateUser("u_hello", user_program_hello, USER_PROG_SIZE(user_program_hello, user_program_hello_end));
            Task_CreateUser("u_count", user_program_counter, USER_PROG_SIZE(user_program_counter, user_program_counter_end));
            Task_CreateUser("u_fault_gp", user_program_fault_gp, USER_PROG_SIZE(user_program_fault_gp, user_program_fault_gp_end));
            Task_CreateUser("u_fault_pf", user_program_fault_pf, USER_PROG_SIZE(user_program_fault_pf, user_program_fault_pf_end));
            shell_print("All 4 user processes queued in MLFQ Queue 0.\n");
        }
        else
        {
            shell_print("Unknown user subcommand '%s'. Type 'user' for options.\n", sub);
        }
    }
    else if (str_cmp(cmd, "disk") == 0)
    {
        shell_print("\n=== Storage Device Subsystem (ATA PIO) ===\n");
        const ATA_DeviceInfo* dev = ATA_GetDevice();
        if (dev && dev->present)
        {
            shell_print("  Primary Master Drive : Detected (IDE Channel 0)\n");
            shell_print("  Model Identifier     : %s\n", dev->model);
            shell_print("  Serial Number        : %s\n", dev->serial);
            shell_print("  Total Sectors (LBA28): %u sectors\n", dev->total_sectors);
            shell_print("  Sector Size          : %u bytes\n", ATA_SECTOR_SIZE);
            shell_print("  Storage Capacity     : %u MB (or %u KB)\n",
                        dev->total_mb, (dev->total_sectors * ATA_SECTOR_SIZE) / 1024);
            shell_print("  48-bit LBA Supported : %s\n", dev->supports_lba48 ? "Yes" : "No");
            shell_print("  Filesystem Mount     : %s\n\n", FAT_IsMounted() ? "FAT Mounted (/)" : "Unmounted");
        }
        else
        {
            shell_print("  No ATA drive detected on Primary Master.\n\n");
        }
    }
    else if (str_cmp(cmd, "ifconfig") == 0)
    {
        NetInterface* netif = Net_GetInterface();
        if (!netif || !netif->link_up)
        {
            shell_print("ifconfig: no active network interface.\n");
        }
        else
        {
            char ipStr[16], maskStr[16], gwStr[16], dnsStr[16];
            ipv4_to_str(netif->ip, ipStr);
            ipv4_to_str(netif->netmask, maskStr);
            ipv4_to_str(netif->gateway, gwStr);
            ipv4_to_str(netif->dns, dnsStr);

            shell_print("\n%s: flags=UP,BROADCAST,MULTICAST\n", netif->name);
            shell_print("      ether %02x:%02x:%02x:%02x:%02x:%02x\n",
                        netif->mac[0], netif->mac[1], netif->mac[2],
                        netif->mac[3], netif->mac[4], netif->mac[5]);
            shell_print("      inet %s  netmask %s  gateway %s\n", ipStr, maskStr, gwStr);
            shell_print("      dns %s\n", dnsStr);
            shell_print("      RX packets: %u  bytes: %u\n", netif->rx_packets, netif->rx_bytes);
            shell_print("      TX packets: %u  bytes: %u\n\n", netif->tx_packets, netif->tx_bytes);
        }
    }
    else if (str_cmp(cmd, "ping") == 0)
    {
        if (argc < 2)
        {
            shell_print("Usage: ping <destination_ip>\n");
        }
        else
        {
            const char* target_str = argv[1];
            uint32_t target_ip = str_to_ipv4(target_str);
            if (target_ip == 0)
            {
                shell_print("ping: invalid IPv4 address '%s'\n", target_str);
            }
            else
            {
                NetInterface* netif = Net_GetInterface();
                shell_print("\nPING %s: 56 data bytes\n", target_str);
                int count = 4;
                int received = 0;
                for (int seq = 1; seq <= count; seq++)
                {
                    int rtt = ICMP_Ping(netif, target_ip, 1000);
                    if (rtt >= 0)
                    {
                        shell_print("64 bytes from %s: icmp_seq=%d time=%d ms\n", target_str, seq, rtt);
                        received++;
                    }
                    else
                    {
                        shell_print("Request timeout for icmp_seq=%d\n", seq);
                    }
                    if (seq < count)
                    {
                        Task_Yield();
                    }
                }
                int loss = ((count - received) * 100) / count;
                shell_print("--- %s ping statistics ---\n", target_str);
                shell_print("%d packets transmitted, %d received, %d%% packet loss\n\n",
                            count, received, loss);
            }
        }
    }
    else if (str_cmp(cmd, "ls") == 0)
    {
        const char* path = (argc > 1) ? argv[1] : "/";
        shell_print("\nDirectory listing of '%s':\n", path);
        shell_print("  TYPE    SIZE (BYTES)  NAME\n");
        shell_print("  --------------------------------------\n");

        VFS_DirEntry entry;
        size_t index = 0;
        int count = 0;

        while (vfs_readdir(path, index, &entry) == 0)
        {
            if (entry.is_directory)
            {
                shell_print("  <DIR>     [DIR]      %s\n", entry.name);
            }
            else
            {
                shell_print("  <FILE>    %u B\t%s\n", entry.size, entry.name);
            }
            index++;
            count++;
        }
        shell_print("  --------------------------------------\n");
        shell_print("  Total entries: %d\n\n", count);
    }
    else if (str_cmp(cmd, "cat") == 0)
    {
        if (argc < 2)
        {
            shell_print("Usage: cat <filepath>\n");
        }
        else
        {
            const char* path = argv[1];
            fd_t fd = vfs_open(path, VFS_O_RDONLY);
            if (fd < 0)
            {
                shell_print("cat: cannot open '%s': No such file or directory\n", path);
            }
            else
            {
                char buf[256];
                int bytesRead;
                while ((bytesRead = vfs_read(fd, buf, sizeof(buf) - 1)) > 0)
                {
                    buf[bytesRead] = '\0';
                    shell_print("%s", buf);
                }
                shell_putc('\n');
                vfs_close(fd);
            }
        }
    }
    else if (str_cmp(cmd, "touch") == 0)
    {
        if (argc < 2)
        {
            shell_print("Usage: touch <filepath>\n");
        }
        else
        {
            const char* path = argv[1];
            if (vfs_touch(path) == 0)
            {
                shell_print("Created file '%s'.\n", path);
            }
            else
            {
                shell_print("touch: failed to create '%s'\n", path);
            }
        }
    }
    else if (str_cmp(cmd, "write") == 0)
    {
        if (argc < 3)
        {
            shell_print("Usage: write <filepath> <text...>\n");
        }
        else
        {
            const char* path = argv[1];
            fd_t fd = vfs_open(path, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_APPEND);
            if (fd < 0)
            {
                shell_print("write: cannot open '%s'\n", path);
            }
            else
            {
                for (int i = 2; i < argc; i++)
                {
                    vfs_write(fd, argv[i], strlen(argv[i]));
                    if (i + 1 < argc)
                        vfs_write(fd, " ", 1);
                }
                vfs_write(fd, "\n", 1);
                vfs_close(fd);
                shell_print("Successfully wrote data to '%s'.\n", path);
            }
        }
    }
    else if (str_cmp(cmd, "reboot") == 0)
    {
        shell_print("Rebooting myOS via 8042 controller pulse...\n");
        for (volatile int j = 0; j < 500000; j++);
        i686_outb(0x64, 0xFE);
    }
    else if (str_cmp(cmd, "exec") == 0)
    {
        if (argc < 2)
        {
            shell_print("Usage: exec <filepath> [args...]\n");
        }
        else
        {
            const char* path = argv[1];
            char* child_argv[MAX_ARGS];
            int child_argc = argc - 1;
            for (int i = 0; i < child_argc; i++)
            {
                child_argv[i] = argv[i + 1];
            }
            child_argv[child_argc] = NULL;
            Shell_ExecuteELF(path, child_argc, child_argv);
        }
    }
    else
    {
        // Check if cmd exists as an executable file on disk:
        // Try exact path, then /bin/<cmd>, then /bin/<cmd>.elf
        char tryPath[64];
        bool found = false;
        VFS_Stat st;

        if (vfs_stat(cmd, &st) == 0 && !st.is_directory)
        {
            strcpy(tryPath, cmd);
            found = true;
        }
        else
        {
            strcpy(tryPath, "/bin/");
            strcat(tryPath, cmd);
            if (vfs_stat(tryPath, &st) == 0 && !st.is_directory)
            {
                found = true;
            }
            else
            {
                strcat(tryPath, ".elf");
                if (vfs_stat(tryPath, &st) == 0 && !st.is_directory)
                {
                    found = true;
                }
            }
        }

        if (found)
        {
            Shell_ExecuteELF(tryPath, argc, argv);
        }
        else
        {
            shell_print("Unknown command: '%s'. Type 'help' for available commands.\n", cmd);
        }
    }
}

void Shell_Task(void)
{
    shell_print("\n");
    shell_print("=======================================================\n");
    shell_print("  myOS Interactive Command Shell (v1.0)\n");
    shell_print("  Type 'help' for available commands.\n");
    shell_print("=======================================================\n\n");

    char lineBuffer[MAX_LINE_LEN];

    while (true)
    {
        shell_print("myos> ");
        size_t len = Shell_ReadLine(lineBuffer, sizeof(lineBuffer));
        if (len > 0)
        {
            Shell_Execute(lineBuffer);
        }
    }
}

void Shell_Initialize(void)
{
    Task_Create("shell", Shell_Task);
}
