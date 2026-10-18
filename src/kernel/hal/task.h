#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <arch/i686/isr.h>

#define MLFQ_LEVELS             3

#define MLFQ_PRIORITY_HIGH      0   // Interactive: Quantum = 2 ticks (~20ms)
#define MLFQ_PRIORITY_MED       1   // Medium:      Quantum = 6 ticks (~60ms)
#define MLFQ_PRIORITY_LOW       2   // Batch/Hog:   Quantum = 15 ticks (~150ms)

#define MLFQ_BOOST_PERIOD       100 // Boost all tasks to Queue 0 every 100 ticks (anti-starvation)

#define TASK_STACK_SIZE         4096
#define TASK_MAGIC              0x7A5C1337

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

    // MLFQ Scheduling Fields
    uint8_t     priority;           // Current MLFQ level: 0, 1, or 2
    uint32_t    ticks_remaining;    // Remaining ticks in current quantum
    uint32_t    total_ticks;        // Lifetime CPU ticks consumed
    uint32_t    voluntary_yields;   // Count of voluntary yields
    uint32_t    preemptions;        // Count of times quantum expired

    Task*       next;               // Intrusive queue pointers
    Task*       prev;
};

// Task Scheduler Public API
void        Task_Initialize(void);
Task*       Task_Create(const char* name, void (*entryPoint)(void));
void        Task_Yield(void);
void        Task_Exit(void);
bool        Task_Kill(uint32_t pid);
Registers*  Task_Schedule(Registers* currentRegs);
Task*       Task_GetCurrent(void);
bool        Task_IsActive(void);
void        Task_DumpStats(void);
