#include "shell.h"
#include <arch/i686/keyboard.h>
#include <arch/i686/io.h>
#include <arch/i686/vga_text.h>
#include <hal/task.h>
#include <hal/pmm.h>
#include <hal/vmm.h>
#include <hal/heap.h>
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
        shell_print("  kill <pid>          - Terminate task by Process ID\n");
        shell_print("  stats               - Dump live MLFQ scheduling metrics\n");
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
    else if (str_cmp(cmd, "reboot") == 0)
    {
        shell_print("Rebooting myOS via 8042 controller pulse...\n");
        for (volatile int j = 0; j < 500000; j++);
        i686_outb(0x64, 0xFE);
    }
    else
    {
        shell_print("Unknown command: '%s'. Type 'help' for available commands.\n", cmd);
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
