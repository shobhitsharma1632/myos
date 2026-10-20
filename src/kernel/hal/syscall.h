#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <arch/i686/isr.h>

// System Call Numbers (standard POSIX/Linux inspired)
#define SYS_EXIT        0   // Terminate current process (ebx = exit code)
#define SYS_PRINT       1   // Print string to output (ebx = string ptr, ecx = length)
#define SYS_READ        2   // Read string from keyboard (ebx = buffer ptr, ecx = max len)
#define SYS_YIELD       3   // Voluntarily yield CPU time slice
#define SYS_GETPID      4   // Get current process ID
#define SYS_SLEEP       5   // Sleep for N ticks (ebx = ticks)
#define SYS_GETTICKS    6   // Get system timer ticks since boot
#define SYS_SBRK        7   // Adjust user heap break (ebx = increment in bytes)
#define SYS_BEEP        8   // PC speaker beep (ebx = freq in Hz, ecx = duration in ms)
#define SYS_OPEN        10  // Open file (ebx = path, ecx = flags)
#define SYS_CLOSE       11  // Close file (ebx = fd)
#define SYS_READ_FILE   12  // Read file (ebx = fd, ecx = buf, edx = count)
#define SYS_WRITE_FILE  13  // Write file (ebx = fd, ecx = buf, edx = count)
#define SYS_READDIR     14  // Read directory (ebx = path, ecx = index, edx = entry ptr)
#define SYS_STAT        15  // File stat (ebx = path, ecx = stat ptr)
#define SYS_SPAWN       16  // Spawn user ELF program (ebx = path, ecx = argc, edx = argv)
#define SYS_WAITPID     17  // Wait for user process (ebx = pid, ecx = status ptr)

// Kernel Syscall Gateway Interface
void Syscall_Initialize(void);
void Syscall_Handler(Registers* regs);
void Syscall_RequestYield(void);
bool Syscall_TakeYieldRequest(void);

// User-space inline assembly wrappers for int 0x80
static inline int32_t syscall0(uint32_t num)
{
    int32_t ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num) : "memory");
    return ret;
}

static inline int32_t syscall1(uint32_t num, uint32_t arg1)
{
    int32_t ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1) : "memory");
    return ret;
}

static inline int32_t syscall2(uint32_t num, uint32_t arg1, uint32_t arg2)
{
    int32_t ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1), "c"(arg2) : "memory");
    return ret;
}

static inline int32_t syscall3(uint32_t num, uint32_t arg1, uint32_t arg2, uint32_t arg3)
{
    int32_t ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3) : "memory");
    return ret;
}

// User-space C library convenience API
static inline int32_t sys_exit(int32_t code)
{
    return syscall1(SYS_EXIT, (uint32_t)code);
}

static inline int32_t sys_print(const char* str)
{
    if (!str) return 0;
    uint32_t len = 0;
    while (str[len]) len++;
    return syscall2(SYS_PRINT, (uint32_t)str, len);
}

static inline int32_t sys_print_len(const char* str, uint32_t len)
{
    return syscall2(SYS_PRINT, (uint32_t)str, len);
}

static inline int32_t sys_read(char* buffer, uint32_t max_len)
{
    return syscall2(SYS_READ, (uint32_t)buffer, max_len);
}

static inline int32_t sys_yield(void)
{
    return syscall0(SYS_YIELD);
}

static inline int32_t sys_getpid(void)
{
    return syscall0(SYS_GETPID);
}

static inline int32_t sys_sleep(uint32_t ticks)
{
    return syscall1(SYS_SLEEP, ticks);
}

static inline uint32_t sys_getticks(void)
{
    return (uint32_t)syscall0(SYS_GETTICKS);
}

static inline void* sys_sbrk(int32_t increment)
{
    return (void*)syscall1(SYS_SBRK, (uint32_t)increment);
}

static inline int32_t sys_beep(uint32_t freq, uint32_t duration_ms)
{
    return syscall2(SYS_BEEP, freq, duration_ms);
}

static inline int32_t sys_open(const char* path, int flags)
{
    return syscall2(SYS_OPEN, (uint32_t)path, (uint32_t)flags);
}

static inline int32_t sys_close(int fd)
{
    return syscall1(SYS_CLOSE, (uint32_t)fd);
}

static inline int32_t sys_read_file(int fd, void* buf, size_t count)
{
    return syscall3(SYS_READ_FILE, (uint32_t)fd, (uint32_t)buf, (uint32_t)count);
}

static inline int32_t sys_write_file(int fd, const void* buf, size_t count)
{
    return syscall3(SYS_WRITE_FILE, (uint32_t)fd, (uint32_t)buf, (uint32_t)count);
}

static inline int32_t sys_readdir(const char* path, size_t index, void* entry)
{
    return syscall3(SYS_READDIR, (uint32_t)path, (uint32_t)index, (uint32_t)entry);
}

static inline int32_t sys_stat(const char* path, void* st)
{
    return syscall2(SYS_STAT, (uint32_t)path, (uint32_t)st);
}

static inline int32_t sys_spawn(const char* path, int argc, char* const argv[])
{
    return syscall3(SYS_SPAWN, (uint32_t)path, (uint32_t)argc, (uint32_t)argv);
}

static inline int32_t sys_waitpid(int32_t pid, int32_t* status)
{
    return syscall2(SYS_WAITPID, (uint32_t)pid, (uint32_t)status);
}
