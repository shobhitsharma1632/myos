#include "syscall.h"
#include <arch/i686/idt.h>
#include <arch/i686/gdt.h>
#include <arch/i686/io.h>
#include <hal/task.h>
#include <hal/vmm.h>
#include <hal/pmm.h>
#include <hal/vfs.h>
#include <stdio.h>
#include <debug.h>

#define MODULE "Syscall"

extern void __attribute((cdecl)) i686_ISR128(void);

static bool g_SyscallYieldRequested = false;

void Syscall_RequestYield(void)
{
    g_SyscallYieldRequested = true;
}

bool Syscall_TakeYieldRequest(void)
{
    if (g_SyscallYieldRequested)
    {
        g_SyscallYieldRequested = false;
        return true;
    }
    return false;
}

void Syscall_Initialize(void)
{
    i686_ISR_RegisterHandler(0x80, Syscall_Handler);

    // Explicitly configure gate 0x80 (128) with IDT_FLAG_RING3 (DPL 3)
    // so software interrupts from Ring 3 succeed without #GP exception.
    i686_IDT_SetGate(0x80, i686_ISR128, i686_GDT_CODE_SEGMENT,
                     IDT_FLAG_PRESENT | IDT_FLAG_RING3 | IDT_FLAG_GATE_32BIT_INT);

    log_info(MODULE, "Syscall dispatcher initialized on int 0x80 with Ring 3 DPL.");
}

void Syscall_Handler(Registers* regs)
{
    uint32_t syscall_num = regs->eax;
    uint32_t arg1        = regs->ebx;
    uint32_t arg2        = regs->ecx;
    uint32_t arg3        = regs->edx;

    Task* current = Task_GetCurrent();

    switch (syscall_num)
    {
        case SYS_EXIT:
        {
            int32_t exit_code = (int32_t)arg1;
            log_info(MODULE, "[SYS_EXIT] Process '%s' (PID %u) terminated with exit code %d.",
                     current ? current->name : "unknown",
                     current ? current->id : 0,
                     exit_code);
            printf("[User Process] '%s' (PID %u) exited with code %d.\n",
                   current ? current->name : "unknown",
                   current ? current->id : 0,
                   exit_code);

            Task_Exit();
            Syscall_RequestYield();
            regs->eax = 0;
            break;
        }

        case SYS_PRINT:
        {
            const char* str = (const char*)arg1;
            uint32_t len    = arg2;

            if (!str)
            {
                regs->eax = 0;
                break;
            }

            // Cap length to reasonable size (e.g. 4096 bytes)
            if (len > 4096) len = 4096;

            uint32_t written = 0;
            for (uint32_t i = 0; i < len; i++)
            {
                char c = str[i];
                if (c == '\0') break;
                putc(c);
                debugc(c);
                written++;
            }
            regs->eax = written;
            break;
        }

        case SYS_READ:
        {
            // Read from keyboard buffer or STDIN
            char* buf = (char*)arg1;
            uint32_t max_len = arg2;
            if (!buf || max_len == 0)
            {
                regs->eax = 0;
                break;
            }
            regs->eax = 0;
            break;
        }

        case SYS_YIELD:
        {
            Syscall_RequestYield();
            regs->eax = 0;
            break;
        }

        case SYS_GETPID:
        {
            regs->eax = current ? current->id : 0;
            break;
        }

        case SYS_SLEEP:
        {
            uint32_t ticks = arg1;
            if (current && ticks > 0)
            {
                current->state = TASK_SLEEPING;
                current->wake_tick = Task_GetTicks() + ticks;
                Syscall_RequestYield();
            }
            regs->eax = 0;
            break;
        }

        case SYS_GETTICKS:
        {
            regs->eax = Task_GetTicks();
            break;
        }

        case SYS_SBRK:
        {
            int32_t increment = (int32_t)arg1;
            if (!current || !current->is_user)
            {
                regs->eax = (uint32_t)-1;
                break;
            }

            uint32_t old_break = current->user_heap_break;
            uint32_t new_break = old_break + increment;

            // Check if expanding into new 4 KiB pages
            uint32_t cur_page = (old_break + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            uint32_t req_page = (new_break + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

            while (cur_page < req_page)
            {
                void* p = PMM_AllocatePage();
                if (!p)
                {
                    regs->eax = (uint32_t)-1;
                    return;
                }
                VMM_MapPage((void*)cur_page, p, PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
                cur_page += PAGE_SIZE;
            }

            current->user_heap_break = new_break;
            regs->eax = old_break;
            break;
        }

        case SYS_BEEP:
        {
            uint32_t freq = arg1;
            uint32_t duration_ms = arg2;
            log_info(MODULE, "[SYS_BEEP] freq=%u Hz, duration=%u ms", freq, duration_ms);
            regs->eax = 0;
            break;
        }

        case SYS_OPEN:
        {
            const char* path = (const char*)arg1;
            int flags = (int)arg2;
            if (!path)
            {
                regs->eax = (uint32_t)-1;
                break;
            }
            regs->eax = (uint32_t)vfs_open(path, flags);
            break;
        }

        case SYS_CLOSE:
        {
            int fd = (int)arg1;
            regs->eax = (uint32_t)vfs_close(fd);
            break;
        }

        case SYS_READ_FILE:
        {
            int fd = (int)arg1;
            void* buf = (void*)arg2;
            size_t count = (size_t)arg3;
            if (!buf)
            {
                regs->eax = (uint32_t)-1;
                break;
            }
            regs->eax = (uint32_t)vfs_read(fd, buf, count);
            break;
        }

        case SYS_WRITE_FILE:
        {
            int fd = (int)arg1;
            const void* buf = (const void*)arg2;
            size_t count = (size_t)arg3;
            if (!buf)
            {
                regs->eax = (uint32_t)-1;
                break;
            }
            regs->eax = (uint32_t)vfs_write(fd, buf, count);
            break;
        }

        case SYS_READDIR:
        {
            const char* path = (const char*)arg1;
            size_t index = (size_t)arg2;
            VFS_DirEntry* entry = (VFS_DirEntry*)arg3;
            if (!entry)
            {
                regs->eax = (uint32_t)-1;
                break;
            }
            regs->eax = (uint32_t)vfs_readdir(path, index, entry);
            break;
        }

        case SYS_STAT:
        {
            const char* path = (const char*)arg1;
            VFS_Stat* st = (VFS_Stat*)arg2;
            if (!st)
            {
                regs->eax = (uint32_t)-1;
                break;
            }
            regs->eax = (uint32_t)vfs_stat(path, st);
            break;
        }

        default:
        {
            log_warn(MODULE, "Unknown syscall %u called by '%s' (PID %u)",
                     syscall_num, current ? current->name : "unknown", current ? current->id : 0);
            regs->eax = (uint32_t)-1;
            break;
        }
    }
}
