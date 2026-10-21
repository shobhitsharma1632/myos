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
#define SYS_SEEK        18  // Seek in file (ebx = fd, ecx = offset, edx = whence)
#define SYS_NET_INFO    19  // Get network interface details (ebx = NetInfoUser ptr)
#define SYS_NET_PING    20  // ICMP ping (ebx = target_ip, ecx = timeout_ms)
#define SYS_NET_SEND_UDP 21 // Send UDP packet (ebx = ip, ecx = port_pair, edx = data_ptr)
#define SYS_PIPE        22  // Create unidirectional pipe (ebx = pipefd[2])
#define SYS_DUP         23  // Duplicate file descriptor (ebx = oldfd)
#define SYS_DUP2        24  // Duplicate file descriptor to specific newfd (ebx = oldfd, ecx = newfd)
#define SYS_SOCKET      25  // Create socket (ebx = domain, ecx = type, edx = protocol)
#define SYS_BIND        26  // Bind socket (ebx = fd, ecx = sockaddr*, edx = socklen)
#define SYS_LISTEN      27  // Listen on socket (ebx = fd, ecx = backlog)
#define SYS_CONNECT     28  // Connect socket (ebx = fd, ecx = sockaddr*, edx = socklen)
#define SYS_ACCEPT      29  // Accept connection (ebx = fd, ecx = sockaddr*, edx = socklen*)
#define SYS_SEND        30  // Send on socket (ebx = fd, ecx = buf, edx = len)
#define SYS_RECV        31  // Receive on socket (ebx = fd, ecx = buf, edx = len)
#define SYS_KILL        32  // Send signal to process (ebx = pid, ecx = sig)
#define SYS_SIGNAL      33  // Set signal handler (ebx = sig, ecx = handler, edx = restorer)
#define SYS_SIGACTION   34  // Set detailed signal action (ebx = sig, ecx = act*, edx = oldact*)
#define SYS_SIGRETURN   35  // Return from signal handler (restores user context)
#define SYS_ALARM       36  // Set alarm timer (ebx = seconds)
#define SYS_PAUSE       37  // Wait for signal (suspends until signal caught)
#define SYS_GUI_CREATE_WIN  38  // Create GUI client window (ebx = title, ecx = (w << 16)|h, edx = (x << 16)|(y & 0xFFFF))
#define SYS_GUI_DESTROY_WIN 39  // Destroy GUI client window (ebx = win_id)
#define SYS_GUI_POLL_EVENT  40  // Poll GUI event (ebx = win_id, ecx = gui_event_t*)
#define SYS_GUI_DAMAGE      41  // Damage / invalidate window rect (ebx = win_id, ecx = (x << 16)|y, edx = (w << 16)|h)
#define SYS_GUI_MAP_BUFFER  42  // Get user-accessible backbuffer pointer (ebx = win_id)
#define SYS_SMP_INFO        43  // Get multi-core SMP topology (ebx = smp_info_t* ptr)

#ifndef _SMP_INFO_T_DEFINED
#define _SMP_INFO_T_DEFINED
typedef struct {
    uint32_t total_cpus;
    uint32_t online_cpus;
    struct {
        uint32_t cpu_id;
        uint32_t lapic_id;
        uint32_t is_bsp;
        uint32_t online;
    } cpus[16];
} smp_info_t;
#endif

#ifndef _GUI_EVENT_T_DEFINED
#define _GUI_EVENT_T_DEFINED
#define GUI_EVENT_NONE         0
#define GUI_EVENT_MOUSE_DOWN   1
#define GUI_EVENT_MOUSE_UP     2
#define GUI_EVENT_MOUSE_MOVE   3
#define GUI_EVENT_KEY_DOWN     4
#define GUI_EVENT_KEY_UP       5
#define GUI_EVENT_FOCUS        6
#define GUI_EVENT_BLUR         7
#define GUI_EVENT_CLOSE        8

typedef struct {
    uint32_t type;          // GUI_EVENT_*
    uint32_t window_id;     // Target window ID
    int32_t  mouse_x;       // Relative to client area
    int32_t  mouse_y;       // Relative to client area
    uint32_t button;        // 1 = Left, 2 = Right
    uint32_t key_char;      // ASCII character
    uint32_t key_code;      // Key scancode / code
    uint32_t modifiers;     // Shift, Ctrl, Alt
} gui_event_t;
#endif

#define AF_UNSPEC 0
#define AF_UNIX   1
#define AF_INET   2

#define SOCK_STREAM 1
#define SOCK_DGRAM  2
#define SOCK_RAW    3

typedef uint32_t socklen_t;

struct in_addr {
    uint32_t s_addr;
};

struct sockaddr {
    uint16_t sa_family;
    char     sa_data[14];
};

struct sockaddr_in {
    uint16_t       sin_family;
    uint16_t       sin_port;
    struct in_addr sin_addr;
    char           sin_zero[8];
};

typedef struct {
    uint8_t  mac[6];
    uint32_t ip;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns;
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t rx_bytes;
    uint32_t tx_bytes;
} NetInfoUser;

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

static inline int32_t sys_lseek(int fd, int32_t offset, int whence)
{
    return syscall3(SYS_SEEK, (uint32_t)fd, (uint32_t)offset, (uint32_t)whence);
}

static inline int32_t sys_net_info(NetInfoUser* info)
{
    return syscall1(SYS_NET_INFO, (uint32_t)info);
}

static inline int32_t sys_net_ping(uint32_t target_ip, uint32_t timeout_ms)
{
    return syscall2(SYS_NET_PING, target_ip, timeout_ms);
}

static inline int32_t sys_pipe(int pipefd[2])
{
    return syscall1(SYS_PIPE, (uint32_t)pipefd);
}

static inline int32_t sys_dup(int oldfd)
{
    return syscall1(SYS_DUP, (uint32_t)oldfd);
}

static inline int32_t sys_dup2(int oldfd, int newfd)
{
    return syscall2(SYS_DUP2, (uint32_t)oldfd, (uint32_t)newfd);
}
