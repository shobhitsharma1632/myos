#include "task.h"
#include <hal/heap.h>
#include <arch/i686/io.h>
#include <arch/i686/irq.h>
#include <memory.h>
#include <stdio.h>
#include <debug.h>

#define MODULE "Task"

// Double-ended intrusive queue for each MLFQ level
typedef struct {
    Task*       head;
    Task*       tail;
    uint32_t    count;
} TaskQueue;

static const uint32_t g_MLFQ_Quanta[MLFQ_LEVELS] = {
    2,   // Queue 0 (High/Interactive): 2 ticks (~20ms)
    6,   // Queue 1 (Medium):           6 ticks (~60ms)
    15   // Queue 2 (Low/Compute):      15 ticks (~150ms)
};

static TaskQueue    g_MLFQ[MLFQ_LEVELS];
static Task*        g_TerminatedHead = NULL;
static Task         g_MainTask;
static Task*        g_IdleTask = NULL;
static Task*        g_CurrentTask = NULL;
static bool         g_SchedulerActive = false;
static uint32_t     g_NextPID = 0;
static uint32_t     g_SchedulerTicks = 0;
static uint32_t     g_LastBoostTick = 0;

// Queue helper functions
static void Queue_PushBack(TaskQueue* q, Task* task)
{
    task->next = NULL;
    task->prev = q->tail;
    if (q->tail)
        q->tail->next = task;
    else
        q->head = task;
    q->tail = task;
    q->count++;
}

static Task* Queue_PopFront(TaskQueue* q)
{
    if (!q->head) return NULL;
    Task* task = q->head;
    q->head = task->next;
    if (q->head)
        q->head->prev = NULL;
    else
        q->tail = NULL;
    task->next = NULL;
    task->prev = NULL;
    q->count--;
    return task;
}

static void Queue_Remove(TaskQueue* q, Task* task)
{
    if (task->prev)
        task->prev->next = task->next;
    else
        q->head = task->next;

    if (task->next)
        task->next->prev = task->prev;
    else
        q->tail = task->prev;

    task->next = NULL;
    task->prev = NULL;
    q->count--;
}

static const char* Task_StateToString(TaskState state)
{
    switch (state)
    {
        case TASK_READY:      return "READY";
        case TASK_RUNNING:    return "RUNNING";
        case TASK_SLEEPING:   return "SLEEPING";
        case TASK_TERMINATED: return "TERMINATED";
        default:              return "UNKNOWN";
    }
}

// Trampoline used as the initial entry point for all forged task stacks
static void Task_Trampoline(void (*entryPoint)(void))
{
    // Enable interrupts now that the task is alive
    i686_EnableInterrupts();

    // Execute user function
    if (entryPoint)
        entryPoint();
    else if (g_CurrentTask && g_CurrentTask->entry)
        g_CurrentTask->entry();

    // If task function returns, cleanly terminate
    Task_Exit();
}

// Background idle task executed when all MLFQ queues are empty
static void Task_IdleFunction(void)
{
    while (true)
    {
        i686_Halt();
    }
}

static void Task_TimerHandler(Registers* regs)
{
    (void)regs;
}

static void Task_ConfigurePIT(uint32_t frequencyHz)
{
    uint32_t divisor = 1193182 / frequencyHz;
    i686_outb(0x43, 0x36); // Channel 0, lobyte/hibyte, Mode 3 (Square Wave)
    i686_outb(0x40, (uint8_t)(divisor & 0xFF));
    i686_outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}

void Task_Initialize(void)
{
    for (int i = 0; i < MLFQ_LEVELS; i++)
    {
        g_MLFQ[i].head = NULL;
        g_MLFQ[i].tail = NULL;
        g_MLFQ[i].count = 0;
    }

    g_TerminatedHead = NULL;
    g_SchedulerTicks = 0;
    g_LastBoostTick = 0;

    // Initialize Main Task (PID 0) representing current kernel execution flow
    memset(&g_MainTask, 0, sizeof(Task));
    g_MainTask.magic = TASK_MAGIC;
    g_MainTask.id = g_NextPID++;
    const char* mainName = "main";
    int i = 0;
    while (mainName[i] && i < 31) {
        g_MainTask.name[i] = mainName[i];
        i++;
    }
    g_MainTask.name[i] = '\0';
    g_MainTask.state = TASK_RUNNING;
    g_MainTask.priority = MLFQ_PRIORITY_HIGH;
    g_MainTask.ticks_remaining = g_MLFQ_Quanta[MLFQ_PRIORITY_HIGH];
    g_MainTask.stack_base = 0; // Boot stack (managed outside heap)
    g_MainTask.stack_size = 0;
    g_MainTask.entry = NULL;

    g_CurrentTask = &g_MainTask;

    // Spawn Idle Task
    g_IdleTask = Task_Create("idle", Task_IdleFunction);
    if (g_IdleTask)
    {
        // Remove idle task from MLFQ ready queue; it is picked only when queues are empty
        Queue_Remove(&g_MLFQ[MLFQ_PRIORITY_HIGH], g_IdleTask);
        g_IdleTask->state = TASK_READY;
    }

    // Configure PIT to 100 Hz (10 ms time-slice unit) and register IRQ 0 on PIC
    Task_ConfigurePIT(100);
    i686_IRQ_RegisterHandler(0, Task_TimerHandler);

    g_SchedulerActive = true;
    log_info(MODULE, "MLFQ Task Scheduler initialized. Main task PID 0 active, 3-level feedback queues ready (PIT 100Hz).");
}

Task* Task_Create(const char* name, void (*entryPoint)(void))
{
    Task* task = (Task*)kmalloc(sizeof(Task));
    if (!task)
    {
        log_err(MODULE, "Failed to allocate TCB for task '%s'!", name ? name : "unnamed");
        return NULL;
    }
    memset(task, 0, sizeof(Task));

    void* stack = kmalloc(TASK_STACK_SIZE);
    if (!stack)
    {
        log_err(MODULE, "Failed to allocate 4 KiB stack for task '%s'!", name ? name : "unnamed");
        kfree(task);
        return NULL;
    }
    memset(stack, 0, TASK_STACK_SIZE);

    task->magic = TASK_MAGIC;
    task->id = g_NextPID++;

    int idx = 0;
    if (name)
    {
        while (name[idx] && idx < 31)
        {
            task->name[idx] = name[idx];
            idx++;
        }
    }
    task->name[idx] = '\0';

    task->state = TASK_READY;
    task->stack_base = (uint32_t)stack;
    task->stack_size = TASK_STACK_SIZE;
    task->entry = entryPoint;

    // MLFQ Initial State: Queue 0 optimism for high responsiveness
    task->priority = MLFQ_PRIORITY_HIGH;
    task->ticks_remaining = g_MLFQ_Quanta[MLFQ_PRIORITY_HIGH];
    task->total_ticks = 0;
    task->voluntary_yields = 0;
    task->preemptions = 0;

    // Forge initial stack frame matching x86 ring-0 interrupt layout:
    // [stack_top - sizeof(Registers)] holds Registers struct.
    // In Registers:
    //   regs->esp is at offset 56: acts as dummy return address (0) for Task_Trampoline
    //   regs->ss  is at offset 60: acts as 1st parameter (entryPoint) for Task_Trampoline
    //   regs->eip is at offset 44: Task_Trampoline
    //   regs->cs  is 0x08 (Kernel Code)
    //   regs->eflags is 0x0202 (IF=1)
    uint8_t* stack_top = (uint8_t*)stack + TASK_STACK_SIZE;
    Registers* regs = (Registers*)(stack_top - sizeof(Registers));
    memset(regs, 0, sizeof(Registers));

    regs->ds = 0x10;                                // Kernel Data Segment
    regs->edi = 0;
    regs->esi = 0;
    regs->ebp = 0;
    regs->esp_original = 0;
    regs->ebx = 0;
    regs->edx = 0;
    regs->ecx = 0;
    regs->eax = 0;
    regs->interrupt = 0;
    regs->error = 0;
    regs->eip = (uint32_t)Task_Trampoline;
    regs->cs = 0x08;                                // Kernel Code Segment
    regs->eflags = 0x0202;                          // IF=1 (interrupts enabled upon iret)
    regs->esp = 0;                                 // Dummy return address for Task_Trampoline
    regs->ss = (uint32_t)entryPoint;               // 1st argument to Task_Trampoline

    task->esp = (uint32_t)regs;

    // Insert task into MLFQ Queue 0
    uint8_t ints = i686_DisableInterrupts();
    Queue_PushBack(&g_MLFQ[MLFQ_PRIORITY_HIGH], task);
    if (ints) i686_EnableInterrupts();

    log_info(MODULE, "Created task '%s' (PID %u) at stack 0x%x, initial ESP 0x%x",
             task->name, task->id, task->stack_base, task->esp);

    return task;
}

void Task_Yield(void)
{
    __asm__ volatile("int $0x81");
}

void Task_Exit(void)
{
    // Atomically transition task to TERMINATED state and enqueue to reaper list
    i686_DisableInterrupts();

    Task* current = g_CurrentTask;
    if (current)
    {
        current->state = TASK_TERMINATED;
        current->next = g_TerminatedHead;
        g_TerminatedHead = current;
    }

    // Yield CPU immediately (software interrupt int $0x81 bypasses IF)
    Task_Yield();

    // Failsafe in case scheduler ever returns to a dead task
    while (1)
    {
        i686_Halt();
    }
}

bool Task_Kill(uint32_t pid)
{
    // System tasks (Main PID 0, Idle PID 1) cannot be killed
    if (pid <= 1)
        return false;

    uint8_t ints = i686_DisableInterrupts();

    // If killing current task, delegate to Task_Exit
    if (g_CurrentTask && g_CurrentTask->id == pid)
    {
        if (ints) i686_EnableInterrupts();
        Task_Exit();
        return true;
    }

    // Search in MLFQ ready queues
    for (int p = 0; p < MLFQ_LEVELS; p++)
    {
        Task* t = g_MLFQ[p].head;
        while (t)
        {
            if (t->id == pid)
            {
                Queue_Remove(&g_MLFQ[p], t);
                t->state = TASK_TERMINATED;
                t->next = g_TerminatedHead;
                g_TerminatedHead = t;

                log_info(MODULE, "Task '%s' (PID %u) terminated by Task_Kill", t->name, t->id);

                if (ints) i686_EnableInterrupts();
                return true;
            }
            t = t->next;
        }
    }

    if (ints) i686_EnableInterrupts();
    return false;
}

Registers* Task_Schedule(Registers* currentRegs)
{
    if (!g_SchedulerActive || g_CurrentTask == NULL)
        return currentRegs;

    // 1. Save execution state of current task
    g_CurrentTask->esp = (uint32_t)currentRegs;
    g_SchedulerTicks++;

    // 2. Rule 5: Priority Boost (Anti-Starvation)
    // Every MLFQ_BOOST_PERIOD ticks, all tasks are boosted to Queue 0
    if (g_SchedulerTicks - g_LastBoostTick >= MLFQ_BOOST_PERIOD)
    {
        g_LastBoostTick = g_SchedulerTicks;

        // Elevate tasks from Queue 1 to Queue 0
        while (g_MLFQ[MLFQ_PRIORITY_MED].head != NULL)
        {
            Task* t = Queue_PopFront(&g_MLFQ[MLFQ_PRIORITY_MED]);
            t->priority = MLFQ_PRIORITY_HIGH;
            t->ticks_remaining = g_MLFQ_Quanta[MLFQ_PRIORITY_HIGH];
            Queue_PushBack(&g_MLFQ[MLFQ_PRIORITY_HIGH], t);
        }

        // Elevate tasks from Queue 2 to Queue 0
        while (g_MLFQ[MLFQ_PRIORITY_LOW].head != NULL)
        {
            Task* t = Queue_PopFront(&g_MLFQ[MLFQ_PRIORITY_LOW]);
            t->priority = MLFQ_PRIORITY_HIGH;
            t->ticks_remaining = g_MLFQ_Quanta[MLFQ_PRIORITY_HIGH];
            Queue_PushBack(&g_MLFQ[MLFQ_PRIORITY_HIGH], t);
        }

        // Elevate running task if still alive
        if (g_CurrentTask->state == TASK_RUNNING)
        {
            g_CurrentTask->priority = MLFQ_PRIORITY_HIGH;
            g_CurrentTask->ticks_remaining = g_MLFQ_Quanta[MLFQ_PRIORITY_HIGH];
        }

        log_info(MODULE, "MLFQ Priority Boost: All tasks elevated to Queue 0 at tick %u", g_SchedulerTicks);
    }

    // 3. Update current task state according to MLFQ Rules 4a & 4b
    bool needReschedule = false;

    if (g_CurrentTask->state == TASK_TERMINATED)
    {
        // Task terminated; already on reaper list, do not re-enqueue
        needReschedule = true;
    }
    else if (g_CurrentTask == g_IdleTask)
    {
        // Idle task running: check if any MLFQ queue has ready work
        for (int p = 0; p < MLFQ_LEVELS; p++)
        {
            if (g_MLFQ[p].head != NULL)
            {
                needReschedule = true;
                break;
            }
        }
        if (!needReschedule)
            return currentRegs; // Keep idling
        g_IdleTask->state = TASK_READY;
    }
    else if (currentRegs->interrupt == 129)
    {
        // Rule 4b: Voluntary yield via Task_Yield()
        // Interactive task optimization: retains priority level
        g_CurrentTask->voluntary_yields++;
        g_CurrentTask->ticks_remaining = g_MLFQ_Quanta[g_CurrentTask->priority];
        g_CurrentTask->state = TASK_READY;
        Queue_PushBack(&g_MLFQ[g_CurrentTask->priority], g_CurrentTask);
        needReschedule = true;
    }
    else // Preemptive timer interrupt (interrupt 32)
    {
        g_CurrentTask->total_ticks++;
        if (g_CurrentTask->ticks_remaining > 0)
            g_CurrentTask->ticks_remaining--;

        if (g_CurrentTask->ticks_remaining == 0)
        {
            // Rule 4a: Quantum consumed without yielding -> demote to lower queue
            g_CurrentTask->preemptions++;
            if (g_CurrentTask->priority < MLFQ_LEVELS - 1)
            {
                g_CurrentTask->priority++;
                log_info(MODULE, "Task '%s' (PID %u) demoted to Queue %u",
                         g_CurrentTask->name, g_CurrentTask->id, g_CurrentTask->priority);
            }
            g_CurrentTask->ticks_remaining = g_MLFQ_Quanta[g_CurrentTask->priority];
            g_CurrentTask->state = TASK_READY;
            Queue_PushBack(&g_MLFQ[g_CurrentTask->priority], g_CurrentTask);
            needReschedule = true;
        }
        else
        {
            // Check if higher priority queue has ready tasks (preemption)
            for (int p = 0; p < g_CurrentTask->priority; p++)
            {
                if (g_MLFQ[p].head != NULL)
                {
                    needReschedule = true;
                    break;
                }
            }

            if (needReschedule)
            {
                g_CurrentTask->state = TASK_READY;
                Queue_PushBack(&g_MLFQ[g_CurrentTask->priority], g_CurrentTask);
            }
            else
            {
                // Continue running current task for remainder of its quantum
                return currentRegs;
            }
        }
    }

    // 4. Select next task to run (Rules 1 & 2: Highest priority queue first, Round-Robin within queue)
    Task* nextTask = NULL;
    for (int p = 0; p < MLFQ_LEVELS; p++)
    {
        if (g_MLFQ[p].head != NULL)
        {
            nextTask = Queue_PopFront(&g_MLFQ[p]);
            break;
        }
    }

    if (nextTask == NULL)
    {
        // No ready tasks: switch to idle task
        nextTask = g_IdleTask;
    }

    // 5. Reaper: safely free stacks and TCBs of dead tasks whose stacks are no longer executing.
    // Note: The CPU is currently executing on g_CurrentTask's stack until Task_Schedule returns
    // and isr_common performs 'mov esp, eax'. Therefore, if g_CurrentTask just terminated,
    // its stack must NOT be freed right now; it will be reaped on the next scheduling event.
    Task* executingTask = g_CurrentTask;
    Task* term = g_TerminatedHead;
    Task* prevTerm = NULL;
    while (term != NULL)
    {
        Task* nextTerm = term->next;
        if (term != executingTask)
        {
            if (prevTerm)
                prevTerm->next = nextTerm;
            else
                g_TerminatedHead = nextTerm;

            log_info(MODULE, "Reaper: Safely freed stack (0x%x) and TCB for terminated task '%s' (PID %u)",
                     term->stack_base, term->name, term->id);

            if (term->stack_base)
                kfree((void*)term->stack_base);
            kfree(term);
        }
        else
        {
            prevTerm = term;
        }
        term = nextTerm;
    }

    // 6. Switch context to nextTask
    g_CurrentTask = nextTask;
    nextTask->state = TASK_RUNNING;

    return (Registers*)nextTask->esp;
}

Task* Task_GetCurrent(void)
{
    return g_CurrentTask;
}

bool Task_IsActive(void)
{
    return g_SchedulerActive;
}

#define STAT_LOG(...) do { printf(__VA_ARGS__); debugf(__VA_ARGS__); } while(0)

void Task_DumpStats(void)
{
    STAT_LOG("\n=== MLFQ Task Scheduler Statistics (Scheduler Tick %u) ===\n", g_SchedulerTicks);

    if (g_CurrentTask)
    {
        STAT_LOG("  * PID %u: %s | Queue: %u | State: %s | Ticks: %u | Yields: %u | Preempts: %u [CURRENT]\n",
                 g_CurrentTask->id, g_CurrentTask->name, g_CurrentTask->priority,
                 Task_StateToString(g_CurrentTask->state),
                 g_CurrentTask->total_ticks, g_CurrentTask->voluntary_yields, g_CurrentTask->preemptions);
    }

    for (int p = 0; p < MLFQ_LEVELS; p++)
    {
        Task* t = g_MLFQ[p].head;
        while (t)
        {
            if (t != g_CurrentTask)
            {
                STAT_LOG("    PID %u: %s | Queue: %u | State: %s | Ticks: %u | Yields: %u | Preempts: %u\n",
                         t->id, t->name, t->priority,
                         Task_StateToString(t->state),
                         t->total_ticks, t->voluntary_yields, t->preemptions);
            }
            t = t->next;
        }
    }
    STAT_LOG("============================================================\n\n");
}
