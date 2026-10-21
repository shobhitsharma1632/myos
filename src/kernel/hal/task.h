#pragma once

#include "signal.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <arch/i686/isr.h>

#define MLFQ_LEVELS             3

#define MLFQ_PRIORITY_HIGH      0   // Interactive: Quantum = 2 ticks (~20ms)
#define MLFQ_PRIORITY_MED       1   // Medium:      Quantum = 6 ticks (~60ms)
#define MLFQ_PRIORITY_LOW       2   // Batch/Hog:   Quantum = 15 ticks (~150ms)

#define MLFQ_BOOST_PERIOD       100 // Boost all tasks to Queue 0 every 100 ticks (anti-starvation)

#define TASK_STACK_SIZE         16384
#define TASK_MAGIC              0x7A5C1337
#define TASK_MAX_FDS            16

typedef enum {
    TASK_READY,
    TASK_RUNNING,
    TASK_SLEEPING,
    TASK_TERMINATED
} TaskState;

typedef struct Task Task;

struct Task {
    uint32_t    magic;              // TASK_MAGIC
    uint32_t    id;                 // Process ID (PID)
    char        name[32];           // Task name
    TaskState   state;              // Execution state
    uint32_t    esp;                // Saved stack pointer (Registers*)
    uint32_t    stack_base;         // Base pointer of allocated 4 KiB stack
    uint32_t    stack_size;         // Stack size (4096 bytes)
    void        (*entry)(void);     // Task entry function pointer

    // User Mode (Ring 3) Specific Fields
    bool        is_user;            // True if Ring 3 user process
    uint32_t    kernel_stack_top;   // Top of kernel stack for TSS esp0
    uint32_t    user_code_phys;     // Physical frame of user code
    uint32_t    user_code_virt;     // Virtual address where user code is mapped
    uint32_t    user_stack_phys;    // Physical frame of user stack
    uint32_t    user_stack_virt;    // Virtual address of user stack page base
    uint32_t    user_heap_break;    // Current program break for sbrk()

    // Dynamic ELF Process Fields
    uint32_t    page_directory_phys; // Physical address of dedicated Page Directory (0 = kernel/legacy slot)
    int32_t     exit_code;           // Exit code reported via SYS_EXIT
    bool        waitable;            // True if process lifecycle retained for Task_WaitPID
    bool        reapable;            // True if zombie status collected and ready for reaper
    size_t      elf_pages_count;
    void*       elf_pages_phys[256];
    size_t      elf_pts_count;
    void*       elf_pts_phys[32];

    // File Descriptor Table (POSIX VFS)
    int         fd_table[TASK_MAX_FDS];

    // POSIX Signal Handling Fields
    uint32_t    pending_signals;        // Bitmask of pending signals (1..31)
    uint32_t    signal_mask;            // Blocked signals bitmask
    uint32_t    signal_handlers[NSIG];  // User signal handler addresses
    uint32_t    signal_restorers[NSIG]; // Restorer trampoline addresses
    uint32_t    alarm_ticks;            // Alarm timer ticks countdown (0 = inactive)
    bool        in_signal_handler;      // True if executing a signal handler

    // MLFQ Scheduling Fields
    uint8_t     priority;           // Current MLFQ level: 0, 1, or 2
    uint32_t    ticks_remaining;    // Remaining ticks in current quantum
    uint32_t    total_ticks;        // Lifetime CPU ticks consumed
    uint32_t    voluntary_yields;   // Count of voluntary yields
    uint32_t    preemptions;        // Count of times quantum expired
    uint32_t    wake_tick;          // Scheduler tick at which sleeping task wakes up

    Task*       next;               // Intrusive queue pointers
    Task*       prev;
};

// Task Scheduler Public API
void        Task_Initialize(void);
Task*       Task_Create(const char* name, void (*entryPoint)(void));
Task*       Task_CreateUser(const char* name, const void* code, size_t codeSize);
Task*       Task_CreateUserFromELF(const char* name, const char* filepath, int argc, char* const argv[]);
Task*       Task_Find(uint32_t pid);
int32_t     Task_WaitPID(uint32_t pid, int32_t* outExitCode);
void        Task_Yield(void);
void        Task_Exit(void);
void        Task_ExitWithCode(int32_t code);
bool        Task_Kill(uint32_t pid);
bool        Task_SendSignal(uint32_t pid, int sig);
void        Task_DeliverSignals(Registers* regs);
Registers*  Task_Schedule(Registers* currentRegs);
Task*       Task_GetCurrent(void);
bool        Task_IsActive(void);
void        Task_DumpStats(void);
uint32_t    Task_GetTicks(void);
