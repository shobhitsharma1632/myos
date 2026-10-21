#include "task.h"
#include "elf_loader.h"
#include "spinlock.h"
#include "smp.h"
#include <hal/vfs.h>
#include <hal/gui.h>
#include <hal/heap.h>
#include <hal/vmm.h>
#include <hal/pmm.h>
#include <arch/i686/io.h>
#include <arch/i686/irq.h>
#include <arch/i686/gdt.h>
#include <arch/i686/tss.h>
#include <arch/i686/apic.h>
#include <memory.h>
#include <string.h>
#include <stdio.h>
#include <debug.h>

#define MODULE "Task"
#define MAX_SMP_CPUS 16
#define MAX_SYSTEM_TASKS 128

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

// Per-CPU Runqueue and execution context
typedef struct {
    uint32_t     cpu_id;
    Task*        current_task;
    Task*        idle_task;
    TaskQueue    mlfq[MLFQ_LEVELS];
    uint32_t     ticks;
    uint32_t     total_tasks_run;
    uint32_t     tasks_stolen_from_others;
    uint32_t     tasks_stolen_by_others;
    uint32_t     voluntary_yields;
    uint32_t     preemptions;
    Spinlock     lock;
} SMP_RunQueue;

static SMP_RunQueue g_PerCPU[MAX_SMP_CPUS];
volatile bool       g_SMP_SchedulerReady = false;

// Global process tracking and synchronization
static Task*        g_AllTasks[MAX_SYSTEM_TASKS];
static Spinlock     g_TaskRegistryLock = SPINLOCK_INIT;
static Spinlock     g_SleepLock = SPINLOCK_INIT;
static Spinlock     g_ReaperLock = SPINLOCK_INIT;

static Task*        g_TerminatedHead = NULL;
static Task*        g_SleepingHead = NULL;
static Task         g_MainTask;
static Task*        g_IdleTask = NULL;
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

static Task* Queue_PopBack(TaskQueue* q)
{
    if (!q->tail) return NULL;
    Task* task = q->tail;
    q->tail = task->prev;
    if (q->tail)
        q->tail->next = NULL;
    else
        q->head = NULL;
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

// Select least loaded CPU for new task placement
static uint32_t Task_SelectLeastLoadedCPU(void)
{
    uint32_t online = SMP_GetOnlineCount();
    if (online <= 1) return 0;

    uint32_t best_cpu = 0;
    uint32_t min_load = 0xFFFFFFFF;

    for (uint32_t c = 0; c < online; c++)
    {
        uint32_t load = g_PerCPU[c].mlfq[0].count + g_PerCPU[c].mlfq[1].count + g_PerCPU[c].mlfq[2].count;
        if (g_PerCPU[c].current_task && g_PerCPU[c].current_task != g_PerCPU[c].idle_task)
            load++;
        if (load < min_load)
        {
            min_load = load;
            best_cpu = c;
        }
    }
    return best_cpu;
}

// Enqueue task into a specific CPU or least loaded CPU
static void Task_Enqueue(Task* task, uint32_t target_cpu)
{
    if (!task) return;

    if (target_cpu >= SMP_GetOnlineCount())
        target_cpu = Task_SelectLeastLoadedCPU();

    task->assigned_cpu = target_cpu;

    uint8_t flags = spinlock_acquire_irqsave(&g_PerCPU[target_cpu].lock);
    Queue_PushBack(&g_PerCPU[target_cpu].mlfq[task->priority], task);
    spinlock_release_irqrestore(&g_PerCPU[target_cpu].lock, flags);

    uint32_t cur_cpu = SMP_GetCurrentCPUID();
    if (target_cpu != cur_cpu)
    {
        SMP_SendRescheduleIPI(target_cpu);
    }
}

// Work-stealing algorithm: steal a task from another CPU's queue
static Task* Task_StealWork(uint32_t my_cpu)
{
    uint32_t online = SMP_GetOnlineCount();
    if (online <= 1) return NULL;

    for (uint32_t offset = 1; offset < online; offset++)
    {
        uint32_t target_cpu = (my_cpu + offset) % online;

        if (spinlock_try_acquire(&g_PerCPU[target_cpu].lock))
        {
            Task* stolen = NULL;

            for (int p = 0; p < MLFQ_LEVELS; p++)
            {
                if (g_PerCPU[target_cpu].mlfq[p].count > 0)
                {
                    stolen = Queue_PopBack(&g_PerCPU[target_cpu].mlfq[p]);
                    if (stolen)
                    {
                        if (stolen->cpu_affinity & (1 << my_cpu))
                        {
                            stolen->assigned_cpu = my_cpu;
                            g_PerCPU[target_cpu].tasks_stolen_by_others++;
                            g_PerCPU[my_cpu].tasks_stolen_from_others++;
                            break;
                        }
                        else
                        {
                            Queue_PushBack(&g_PerCPU[target_cpu].mlfq[p], stolen);
                            stolen = NULL;
                        }
                    }
                }
            }

            spinlock_release(&g_PerCPU[target_cpu].lock);

            if (stolen)
            {
                log_debug(MODULE, "CPU %u stole task '%s' (PID %u) from CPU %u",
                          my_cpu, stolen->name, stolen->id, target_cpu);
                return stolen;
            }
        }
    }

    return NULL;
}

// Trampoline used as the initial entry point for all forged task stacks
static void Task_Trampoline(void (*entryPoint)(void))
{
    // Enable interrupts now that the task is alive
    i686_EnableInterrupts();

    Task* cur = Task_GetCurrent();

    // Execute user function
    if (entryPoint)
        entryPoint();
    else if (cur && cur->entry)
        cur->entry();

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

void Task_APInit(uint32_t cpu_id)
{
    if (cpu_id >= MAX_SMP_CPUS) return;

    SMP_RunQueue* rq = &g_PerCPU[cpu_id];
    rq->cpu_id = cpu_id;
    spinlock_init(&rq->lock);
    for (int p = 0; p < MLFQ_LEVELS; p++)
    {
        rq->mlfq[p].head = NULL;
        rq->mlfq[p].tail = NULL;
        rq->mlfq[p].count = 0;
    }
    rq->ticks = 0;
    rq->total_tasks_run = 0;
    rq->tasks_stolen_from_others = 0;
    rq->tasks_stolen_by_others = 0;
    rq->voluntary_yields = 0;
    rq->preemptions = 0;

    // Create dedicated idle task for this AP: idle-<cpu_id>
    char idleName[16];
    snprintf(idleName, sizeof(idleName), "idle-%u", cpu_id);
    Task* idle = Task_CreateOnCPU(idleName, Task_IdleFunction, cpu_id);
    if (idle)
    {
        uint8_t flags = spinlock_acquire_irqsave(&rq->lock);
        Queue_Remove(&rq->mlfq[MLFQ_PRIORITY_HIGH], idle);
        idle->state = TASK_READY;
        rq->idle_task = idle;
        rq->current_task = NULL; // NULL until first context switch
        spinlock_release_irqrestore(&rq->lock, flags);
    }
}

void Task_APStart(uint32_t cpu_id)
{
    log_info(MODULE, "AP Core %u entering parallel MLFQ scheduler...", cpu_id);
    Task_Yield();
    while (1)
    {
        i686_Halt();
    }
}

void Task_Initialize(void)
{
    for (int c = 0; c < MAX_SMP_CPUS; c++)
    {
        g_PerCPU[c].cpu_id = c;
        spinlock_init(&g_PerCPU[c].lock);
        for (int p = 0; p < MLFQ_LEVELS; p++)
        {
            g_PerCPU[c].mlfq[p].head = NULL;
            g_PerCPU[c].mlfq[p].tail = NULL;
            g_PerCPU[c].mlfq[p].count = 0;
        }
        g_PerCPU[c].current_task = NULL;
        g_PerCPU[c].idle_task = NULL;
        g_PerCPU[c].ticks = 0;
        g_PerCPU[c].total_tasks_run = 0;
        g_PerCPU[c].tasks_stolen_from_others = 0;
        g_PerCPU[c].tasks_stolen_by_others = 0;
        g_PerCPU[c].voluntary_yields = 0;
        g_PerCPU[c].preemptions = 0;
    }

    memset(g_AllTasks, 0, sizeof(g_AllTasks));
    g_TerminatedHead = NULL;
    g_SleepingHead = NULL;
    g_SchedulerTicks = 0;
    g_LastBoostTick = 0;

    // Initialize Main Task (PID 0) representing current kernel execution flow on BSP (Core 0)
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
    void* main_kstack = kmalloc(TASK_STACK_SIZE);
    g_MainTask.kernel_stack_top = (uint32_t)main_kstack + TASK_STACK_SIZE;
    g_MainTask.entry = NULL;
    g_MainTask.assigned_cpu = 0;
    g_MainTask.cpu_affinity = 0x1; // Pin BSP main task strictly to Core 0
    vfs_init_task_fds(&g_MainTask);

    g_AllTasks[0] = &g_MainTask;
    g_PerCPU[0].current_task = &g_MainTask;

    // Spawn Idle Task for BSP: idle-0
    g_IdleTask = Task_CreateOnCPU("idle-0", Task_IdleFunction, 0);
    if (g_IdleTask)
    {
        uint8_t flags = spinlock_acquire_irqsave(&g_PerCPU[0].lock);
        Queue_Remove(&g_PerCPU[0].mlfq[MLFQ_PRIORITY_HIGH], g_IdleTask);
        g_IdleTask->state = TASK_READY;
        g_PerCPU[0].idle_task = g_IdleTask;
        spinlock_release_irqrestore(&g_PerCPU[0].lock, flags);
    }

    // Configure PIT to 100 Hz (10 ms time-slice unit) and register IRQ 0 on PIC
    Task_ConfigurePIT(100);
    i686_IRQ_RegisterHandler(0, Task_TimerHandler);

    g_SchedulerActive = true;
    g_SMP_SchedulerReady = false; // APs released when SMP_Initialize completes

    log_info(MODULE, "MLFQ Multi-Core Scheduler initialized. BSP Core 0 active (%u cores online), 3-level feedback queues ready.",
             SMP_GetOnlineCount());
}

Task* Task_CreateOnCPU(const char* name, void (*entryPoint)(void), uint32_t cpu_id)
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
        log_err(MODULE, "Failed to allocate 16 KiB stack for task '%s'!", name ? name : "unnamed");
        kfree(task);
        return NULL;
    }
    memset(stack, 0, TASK_STACK_SIZE);

    task->magic = TASK_MAGIC;
    task->cpu_affinity = (cpu_id < MAX_SMP_CPUS) ? (1 << cpu_id) : 0xFFFFFFFF;
    task->assigned_cpu = (cpu_id < MAX_SMP_CPUS) ? cpu_id : Task_SelectLeastLoadedCPU();

    uint8_t regFlags = spinlock_acquire_irqsave(&g_TaskRegistryLock);
    task->id = g_NextPID++;
    for (int s = 0; s < MAX_SYSTEM_TASKS; s++)
    {
        if (g_AllTasks[s] == NULL)
        {
            g_AllTasks[s] = task;
            break;
        }
    }
    spinlock_release_irqrestore(&g_TaskRegistryLock, regFlags);

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
    task->is_user = false;
    task->kernel_stack_top = (uint32_t)stack + TASK_STACK_SIZE;
    vfs_init_task_fds(task);

    // MLFQ Initial State: Queue 0 optimism for high responsiveness
    task->priority = MLFQ_PRIORITY_HIGH;
    task->ticks_remaining = g_MLFQ_Quanta[MLFQ_PRIORITY_HIGH];
    task->total_ticks = 0;
    task->voluntary_yields = 0;
    task->preemptions = 0;

    uint8_t* stack_top = (uint8_t*)stack + TASK_STACK_SIZE;
    Registers* regs = (Registers*)(stack_top - sizeof(Registers));
    memset(regs, 0, sizeof(Registers));

    regs->ds = 0x10;
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
    regs->cs = 0x08;
    regs->eflags = 0x0202; // IF=1
    regs->esp = 0;
    regs->ss = (uint32_t)entryPoint;

    task->esp = (uint32_t)regs;

    Task_Enqueue(task, task->assigned_cpu);

    log_info(MODULE, "Created task '%s' (PID %u) on CPU %u at stack 0x%x, initial ESP 0x%x",
             task->name, task->id, task->assigned_cpu, task->stack_base, task->esp);

    return task;
}

Task* Task_Create(const char* name, void (*entryPoint)(void))
{
    return Task_CreateOnCPU(name, entryPoint, 0xFFFFFFFF);
}
Task* Task_CreateUser(const char* name, const void* code, size_t codeSize)
{
    Task* task = (Task*)kmalloc(sizeof(Task));
    if (!task)
    {
        log_err(MODULE, "Failed to allocate TCB for user task '%s'!", name ? name : "unnamed");
        return NULL;
    }
    memset(task, 0, sizeof(Task));

    // Allocate 16 KiB kernel stack for privilege transitions (Ring 3 -> Ring 0)
    void* kstack = kmalloc(TASK_STACK_SIZE);
    if (!kstack)
    {
        log_err(MODULE, "Failed to allocate kernel stack for user task '%s'!", name ? name : "unnamed");
        kfree(task);
        return NULL;
    }
    memset(kstack, 0, TASK_STACK_SIZE);

    task->magic = TASK_MAGIC;
    task->cpu_affinity = 0xFFFFFFFF;
    task->assigned_cpu = Task_SelectLeastLoadedCPU();

    uint8_t regFlags = spinlock_acquire_irqsave(&g_TaskRegistryLock);
    task->id = g_NextPID++;
    for (int s = 0; s < MAX_SYSTEM_TASKS; s++)
    {
        if (g_AllTasks[s] == NULL)
        {
            g_AllTasks[s] = task;
            break;
        }
    }
    spinlock_release_irqrestore(&g_TaskRegistryLock, regFlags);

    task->is_user = true;

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
    task->stack_base = (uint32_t)kstack;
    task->stack_size = TASK_STACK_SIZE;
    task->kernel_stack_top = (uint32_t)kstack + TASK_STACK_SIZE;

    // MLFQ Initial State: Queue 0 for interactive responsiveness
    task->priority = MLFQ_PRIORITY_HIGH;
    task->ticks_remaining = g_MLFQ_Quanta[MLFQ_PRIORITY_HIGH];
    task->total_ticks = 0;
    task->voluntary_yields = 0;
    task->preemptions = 0;

    // Allocate and map user-space virtual memory (1 GiB boundary: 0x40000000)
    uint32_t user_slot_base = 0x40000000 + (task->id * 0x20000);
    void* user_code_virt   = (void*)user_slot_base;
    void* user_stack_virt  = (void*)(user_slot_base + 0x10000);
    void* user_stack_top   = (void*)(user_slot_base + 0x11000);
    void* user_heap_start  = (void*)(user_slot_base + 0x12000);

    void* code_phys = PMM_AllocatePage();
    if (!code_phys)
    {
        log_err(MODULE, "Failed to allocate physical page for user code!");
        kfree(kstack);
        kfree(task);
        return NULL;
    }

    void* stack_phys = PMM_AllocatePage();
    if (!stack_phys)
    {
        log_err(MODULE, "Failed to allocate physical page for user stack!");
        PMM_FreePage(code_phys);
        kfree(kstack);
        kfree(task);
        return NULL;
    }

    // Map user code and user stack with PAGE_PRESENT | PAGE_WRITE | PAGE_USER
    if (!VMM_MapPage(user_code_virt, code_phys, PAGE_PRESENT | PAGE_WRITE | PAGE_USER))
    {
        log_err(MODULE, "Failed to map user code page!");
        PMM_FreePage(stack_phys);
        PMM_FreePage(code_phys);
        kfree(kstack);
        kfree(task);
        return NULL;
    }

    if (!VMM_MapPage(user_stack_virt, stack_phys, PAGE_PRESENT | PAGE_WRITE | PAGE_USER))
    {
        log_err(MODULE, "Failed to map user stack page!");
        VMM_UnmapPage(user_code_virt);
        PMM_FreePage(stack_phys);
        PMM_FreePage(code_phys);
        kfree(kstack);
        kfree(task);
        return NULL;
    }

    memset(user_code_virt, 0, PAGE_SIZE);
    memset(user_stack_virt, 0, PAGE_SIZE);

    if (code && codeSize > 0)
    {
        size_t copyLen = codeSize < PAGE_SIZE ? codeSize : PAGE_SIZE;
        memcpy(user_code_virt, code, copyLen);
    }

    task->user_code_phys  = (uint32_t)code_phys;
    task->user_code_virt  = (uint32_t)user_code_virt;
    task->user_stack_phys = (uint32_t)stack_phys;
    task->user_stack_virt = (uint32_t)user_stack_virt;
    task->user_heap_break = (uint32_t)user_heap_start;
    task->entry           = (void (*)(void))user_code_virt;
    vfs_init_task_fds(task);

    // Forge Ring 3 interrupt stack frame on kernel stack
    uint8_t* kstack_top = (uint8_t*)task->kernel_stack_top;
    Registers* regs = (Registers*)(kstack_top - sizeof(Registers));
    memset(regs, 0, sizeof(Registers));

    regs->ds           = USER_DATA_SELECTOR;           // 0x23 (Ring 3 User Data Segment)
    regs->edi          = 0;
    regs->esi          = 0;
    regs->ebp          = 0;
    regs->esp_original = 0;
    regs->ebx          = 0;
    regs->edx          = 0;
    regs->ecx          = 0;
    regs->eax          = 0;
    regs->interrupt    = 0;
    regs->error        = 0;
    regs->eip          = (uint32_t)user_code_virt;    // User code entry point
    regs->cs           = USER_CODE_SELECTOR;           // 0x1B (Ring 3 User Code Segment)
    regs->eflags       = 0x0202;                       // IF=1 (interrupts enabled upon iret)
    regs->esp          = (uint32_t)user_stack_top - 16; // User Stack Pointer (16-byte aligned)
    regs->ss           = USER_DATA_SELECTOR;           // 0x23 (Ring 3 User Stack Segment)

    task->esp = (uint32_t)regs;

    Task_Enqueue(task, task->assigned_cpu);

    log_info(MODULE, "Created User Process '%s' (PID %u) on CPU %u: Code @ 0x%x, Stack @ 0x%x, KStack @ 0x%x",
             task->name, task->id, task->assigned_cpu, task->user_code_virt, regs->esp, task->kernel_stack_top);

    return task;
}

void Task_Yield(void)
{
    __asm__ volatile("int $0x81");
}

void Task_ExitWithCode(int32_t code)
{
    i686_DisableInterrupts();

    Task* current = Task_GetCurrent();
    if (current)
    {
        vfs_close_task_fds(current);
        current->exit_code = code;
        current->state = TASK_TERMINATED;
    }

    // Yield CPU immediately (software interrupt int $0x81 bypasses IF)
    Task_Yield();

    // Failsafe in case scheduler ever returns to a dead task
    while (1)
    {
        i686_Halt();
    }
}

void Task_Exit(void)
{
    Task_ExitWithCode(0);
}

bool Task_Kill(uint32_t pid)
{
    if (pid <= 1)
        return false;

    Task* cur = Task_GetCurrent();
    if (cur && cur->id == pid)
    {
        Task_Exit();
        return true;
    }

    Task* t = Task_Find(pid);
    if (!t) return false;

    uint32_t target_cpu = t->assigned_cpu;
    if (target_cpu < MAX_SMP_CPUS)
    {
        uint8_t flags = spinlock_acquire_irqsave(&g_PerCPU[target_cpu].lock);
        for (int p = 0; p < MLFQ_LEVELS; p++)
        {
            Task* curr = g_PerCPU[target_cpu].mlfq[p].head;
            while (curr)
            {
                if (curr == t)
                {
                    Queue_Remove(&g_PerCPU[target_cpu].mlfq[p], t);
                    vfs_close_task_fds(t);
                    t->state = TASK_TERMINATED;
                    t->reapable = true;

                    uint8_t rFlags = spinlock_acquire_irqsave(&g_ReaperLock);
                    t->next = g_TerminatedHead;
                    g_TerminatedHead = t;
                    spinlock_release_irqrestore(&g_ReaperLock, rFlags);

                    spinlock_release_irqrestore(&g_PerCPU[target_cpu].lock, flags);
                    log_info(MODULE, "Task '%s' (PID %u) terminated by Task_Kill", t->name, t->id);
                    return true;
                }
                curr = curr->next;
            }
        }
        spinlock_release_irqrestore(&g_PerCPU[target_cpu].lock, flags);
    }

    t->state = TASK_TERMINATED;
    t->reapable = true;
    SMP_SendRescheduleIPI(target_cpu);
    return true;
}

bool Task_SendSignal(uint32_t pid, int sig)
{
    if (sig <= 0 || sig >= NSIG)
        return false;

    Task* t = Task_Find(pid);
    if (!t || t->state == TASK_TERMINATED)
        return false;

    // SIGKILL cannot be caught or ignored
    if (sig == SIGKILL)
    {
        return Task_Kill(pid);
    }

    t->pending_signals |= (1 << sig);

    if (t->state == TASK_SLEEPING)
    {
        uint8_t sFlags = spinlock_acquire_irqsave(&g_SleepLock);
        Task* curr = g_SleepingHead;
        Task* prev = NULL;
        while (curr)
        {
            if (curr == t)
            {
                if (prev)
                    prev->next = curr->next;
                else
                    g_SleepingHead = curr->next;
                break;
            }
            prev = curr;
            curr = curr->next;
        }
        spinlock_release_irqrestore(&g_SleepLock, sFlags);

        t->state = TASK_READY;
        t->next = NULL;
        t->prev = NULL;
        Task_Enqueue(t, t->assigned_cpu);
    }
    else
    {
        if (t->assigned_cpu != SMP_GetCurrentCPUID())
        {
            SMP_SendRescheduleIPI(t->assigned_cpu);
        }
    }

    return true;
}

Task* Task_Find(uint32_t pid)
{
    uint8_t regFlags = spinlock_acquire_irqsave(&g_TaskRegistryLock);
    for (int s = 0; s < MAX_SYSTEM_TASKS; s++)
    {
        if (g_AllTasks[s] && g_AllTasks[s]->id == pid)
        {
            Task* t = g_AllTasks[s];
            spinlock_release_irqrestore(&g_TaskRegistryLock, regFlags);
            return t;
        }
    }
    spinlock_release_irqrestore(&g_TaskRegistryLock, regFlags);
    return NULL;
}

int32_t Task_WaitPID(uint32_t pid, int32_t* outExitCode)
{
    while (true)
    {
        Task* t = Task_Find(pid);
        if (!t)
        {
            return -1;
        }

        if (t->state == TASK_TERMINATED)
        {
            if (outExitCode)
                *outExitCode = t->exit_code;
            t->reapable = true;
            return (int32_t)pid;
        }

        Task_Yield();
    }
}

Task* Task_CreateUserFromELF(const char* name, const char* filepath, int argc, char* const argv[])
{
    ELF_ProgramImage* image = (ELF_ProgramImage*)kmalloc(sizeof(ELF_ProgramImage));
    if (!image)
    {
        log_err(MODULE, "Task_CreateUserFromELF: failed to allocate memory for image structure");
        return NULL;
    }

    if (!ELF_Load(filepath, image))
    {
        log_err(MODULE, "Task_CreateUserFromELF: failed to load ELF '%s'", filepath);
        kfree(image);
        return NULL;
    }

    Task* task = (Task*)kmalloc(sizeof(Task));
    if (!task)
    {
        ELF_Unload(image);
        kfree(image);
        return NULL;
    }
    memset(task, 0, sizeof(Task));

    // Allocate kernel stack for privilege transitions (Ring 3 -> Ring 0)
    void* kstack = kmalloc(TASK_STACK_SIZE);
    if (!kstack)
    {
        kfree(task);
        ELF_Unload(image);
        kfree(image);
        return NULL;
    }
    memset(kstack, 0, TASK_STACK_SIZE);

    task->magic = TASK_MAGIC;
    task->cpu_affinity = 0xFFFFFFFF;
    task->assigned_cpu = Task_SelectLeastLoadedCPU();

    uint8_t regFlags = spinlock_acquire_irqsave(&g_TaskRegistryLock);
    task->id = g_NextPID++;
    for (int s = 0; s < MAX_SYSTEM_TASKS; s++)
    {
        if (g_AllTasks[s] == NULL)
        {
            g_AllTasks[s] = task;
            break;
        }
    }
    spinlock_release_irqrestore(&g_TaskRegistryLock, regFlags);

    task->is_user = true;

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
    task->stack_base = (uint32_t)kstack;
    task->stack_size = TASK_STACK_SIZE;
    task->kernel_stack_top = (uint32_t)kstack + TASK_STACK_SIZE;
    task->page_directory_phys = image->page_directory_phys;
    vfs_clone_task_fds(Task_GetCurrent(), task);
    task->entry = (void (*)(void))image->entry_point;
    task->user_heap_break = image->user_heap_break;
    task->waitable = true;
    task->reapable = false;

    // Track physical pages for cleanup on exit
    task->elf_pages_count = image->pages_count;
    for (size_t i = 0; i < image->pages_count; i++)
    {
        task->elf_pages_phys[i] = image->pages_phys[i];
    }
    task->elf_pts_count = image->pts_count;
    for (size_t i = 0; i < image->pts_count; i++)
    {
        task->elf_pts_phys[i] = image->pts_phys[i];
    }

    task->priority = MLFQ_PRIORITY_HIGH;
    task->ticks_remaining = g_MLFQ_Quanta[MLFQ_PRIORITY_HIGH];

    // Find the top stack page physical frame to format initial user stack
    void* top_stack_phys = image->pages_phys[image->pages_count - 1];
    uint32_t top_stack_virt = USER_STACK_TOP; // 0x40100000
    uint32_t page_base_virt = top_stack_virt - PAGE_SIZE; // 0x400FF000

    // Setup arguments on user stack in standard System V ABI format
    uint32_t curr_sp = top_stack_virt;
    uint32_t argv_ptrs[16];
    int actual_argc = argc > 16 ? 16 : argc;

    for (int i = actual_argc - 1; i >= 0; i--)
    {
        const char* arg = (argv && argv[i]) ? argv[i] : "";
        size_t len = strlen(arg) + 1;
        curr_sp -= len;
        uint32_t page_offset = curr_sp - page_base_virt;
        memcpy((uint8_t*)top_stack_phys + page_offset, arg, len);
        argv_ptrs[i] = curr_sp;
    }

    // 4-byte align the stack pointer
    curr_sp &= ~3;

    // Place NULL-terminated argv array
    curr_sp -= 4; // argv[actual_argc] = NULL
    *(uint32_t*)((uint8_t*)top_stack_phys + (curr_sp - page_base_virt)) = 0;

    for (int i = actual_argc - 1; i >= 0; i--)
    {
        curr_sp -= 4;
        *(uint32_t*)((uint8_t*)top_stack_phys + (curr_sp - page_base_virt)) = argv_ptrs[i];
    }
    uint32_t argv_array_user_ptr = curr_sp;

    // Setup call frame for _start:
    curr_sp -= 4;
    *(uint32_t*)((uint8_t*)top_stack_phys + (curr_sp - page_base_virt)) = argv_array_user_ptr;

    curr_sp -= 4;
    *(uint32_t*)((uint8_t*)top_stack_phys + (curr_sp - page_base_virt)) = (uint32_t)actual_argc;

    curr_sp -= 4;
    *(uint32_t*)((uint8_t*)top_stack_phys + (curr_sp - page_base_virt)) = 0; // return address

    // Forge Ring 3 interrupt stack frame on kernel stack
    uint8_t* kstack_top = (uint8_t*)task->kernel_stack_top;
    Registers* regs = (Registers*)(kstack_top - sizeof(Registers));
    memset(regs, 0, sizeof(Registers));

    regs->ds           = USER_DATA_SELECTOR;           // 0x23 (Ring 3 User Data Segment)
    regs->edi          = 0;
    regs->esi          = 0;
    regs->ebp          = 0;
    regs->esp_original = 0;
    regs->ebx          = 0;
    regs->edx          = 0;
    regs->ecx          = 0;
    regs->eax          = 0;
    regs->interrupt    = 0;
    regs->error        = 0;
    regs->eip          = image->entry_point;           // ELF entry point address
    regs->cs           = USER_CODE_SELECTOR;           // 0x1B (Ring 3 User Code Segment)
    regs->eflags       = 0x0202;                       // IF=1
    regs->esp          = curr_sp;                      // User stack pointer
    regs->ss           = USER_DATA_SELECTOR;           // 0x23 (Ring 3 User Stack Segment)

    task->esp = (uint32_t)regs;

    // Free temporary ELF loading descriptor
    kfree(image);

    Task_Enqueue(task, task->assigned_cpu);

    log_info(MODULE, "Created ELF User Process '%s' (PID %u) on CPU %u: Entry @ 0x%x, Stack @ 0x%x, PD @ 0x%x",
             task->name, task->id, task->assigned_cpu, regs->eip, regs->esp, task->page_directory_phys);

    return task;
}

static void Task_UpdateAlarms(void)
{
    uint8_t regFlags = spinlock_acquire_irqsave(&g_TaskRegistryLock);
    for (int s = 0; s < MAX_SYSTEM_TASKS; s++)
    {
        Task* t = g_AllTasks[s];
        if (t && t->state != TASK_TERMINATED && t->alarm_ticks > 0)
        {
            t->alarm_ticks--;
            if (t->alarm_ticks == 0)
            {
                Task_SendSignal(t->id, SIGALRM);
            }
        }
    }
    spinlock_release_irqrestore(&g_TaskRegistryLock, regFlags);
}

void Task_DeliverSignals(Registers* regs)
{
    if (!regs) return;

    // Only deliver signals when returning to Ring 3 User Mode
    if ((regs->cs & 3) != 3)
        return;

    Task* task = Task_GetCurrent();
    if (!task || !task->is_user)
        return;

    // Do not re-enter if already executing a signal handler
    if (task->in_signal_handler)
        return;

    uint32_t active = task->pending_signals & ~task->signal_mask;
    if (active == 0)
        return;

    // Find lowest active signal (1..31)
    int sig = 0;
    for (int s = 1; s < NSIG; s++)
    {
        if (active & (1 << s))
        {
            sig = s;
            break;
        }
    }

    if (sig == 0)
        return;

    task->pending_signals &= ~(1 << sig);

    uint32_t handler = task->signal_handlers[sig];

    // SIG_IGN: Ignore signal
    if (handler == (uint32_t)SIG_IGN)
        return;

    // SIG_DFL: Default Action
    if (handler == (uint32_t)SIG_DFL)
    {
        if (sig == SIGCHLD || sig == SIGCONT || sig == SIGUSR1 || sig == SIGUSR2)
        {
            return;
        }

        if (sig == SIGSTOP || sig == SIGTSTP)
        {
            task->state = TASK_SLEEPING;
            task->wake_tick = 0xFFFFFFFF;
            return;
        }

        log_info(MODULE, "Process '%s' (PID %u) terminated by default signal %d", task->name, task->id, sig);
        Task_ExitWithCode(128 + sig);
        return;
    }

    // Custom Signal Handler: Forge SigFrame on User Stack
    uint32_t old_sp = regs->esp;
    uint32_t frame_addr = (old_sp - sizeof(SigFrame)) & ~0xF; // 16-byte aligned

    if (frame_addr < 0x40000000 || frame_addr >= 0x80000000)
    {
        log_err(MODULE, "Process '%s' (PID %u) user stack overflow/invalid SP 0x%x on signal %d!",
                task->name, task->id, frame_addr, sig);
        Task_ExitWithCode(128 + SIGSEGV);
        return;
    }

    SigFrame frame;
    memset(&frame, 0, sizeof(SigFrame));

    frame.restorer_return_addr = task->signal_restorers[sig];
    frame.sig = (uint32_t)sig;

    frame.context.eip      = regs->eip;
    frame.context.cs       = regs->cs;
    frame.context.eflags   = regs->eflags;
    frame.context.eax      = regs->eax;
    frame.context.ecx      = regs->ecx;
    frame.context.edx      = regs->edx;
    frame.context.ebx      = regs->ebx;
    frame.context.esp      = old_sp;
    frame.context.ebp      = regs->ebp;
    frame.context.esi      = regs->esi;
    frame.context.edi      = regs->edi;
    frame.context.ds       = regs->ds;
    frame.context.es       = regs->ds;
    frame.context.fs       = regs->ds;
    frame.context.gs       = regs->ds;
    frame.context.old_mask = task->signal_mask;
    frame.context.sig      = (uint32_t)sig;

    memcpy((void*)frame_addr, &frame, sizeof(SigFrame));

    regs->esp = frame_addr;
    regs->eip = handler;

    task->in_signal_handler = true;
    task->signal_mask |= (1 << sig);
}

Registers* Task_Schedule(Registers* currentRegs)
{
    if (!g_SchedulerActive)
        return currentRegs;

    uint32_t cpu_id = SMP_GetCurrentCPUID();
    if (cpu_id >= MAX_SMP_CPUS)
        return currentRegs;

    SMP_RunQueue* my_rq = &g_PerCPU[cpu_id];
    Task* curTask = my_rq->current_task;

    if (curTask)
    {
        curTask->esp = (uint32_t)currentRegs;
    }

    my_rq->ticks++;

    // 1. BSP periodic timer tick housekeeping
    if (cpu_id == 0 && (currentRegs->interrupt == 32 || currentRegs->interrupt == 129))
    {
        if (currentRegs->interrupt == 32)
        {
            g_SchedulerTicks++;
            Task_UpdateAlarms();

            extern void Net_Poll(void);
            Net_Poll();

            // Wake up sleeping tasks whose timer expired
            uint8_t sleepFlags = spinlock_acquire_irqsave(&g_SleepLock);
            Task* sleepTask = g_SleepingHead;
            Task* prevSleep = NULL;
            while (sleepTask != NULL)
            {
                Task* nextSleep = sleepTask->next;
                if (g_SchedulerTicks >= sleepTask->wake_tick)
                {
                    if (prevSleep)
                        prevSleep->next = nextSleep;
                    else
                        g_SleepingHead = nextSleep;

                    sleepTask->next = NULL;
                    sleepTask->prev = NULL;
                    sleepTask->state = TASK_READY;

                    Task_Enqueue(sleepTask, 0xFFFFFFFF);
                }
                else
                {
                    prevSleep = sleepTask;
                }
                sleepTask = nextSleep;
            }
            spinlock_release_irqrestore(&g_SleepLock, sleepFlags);

            // Anti-starvation Priority Boost across all CPUs
            if (g_SchedulerTicks - g_LastBoostTick >= MLFQ_BOOST_PERIOD)
            {
                g_LastBoostTick = g_SchedulerTicks;
                for (uint32_t c = 0; c < SMP_GetOnlineCount(); c++)
                {
                    uint8_t qFlags = spinlock_acquire_irqsave(&g_PerCPU[c].lock);
                    while (g_PerCPU[c].mlfq[MLFQ_PRIORITY_MED].head != NULL)
                    {
                        Task* t = Queue_PopFront(&g_PerCPU[c].mlfq[MLFQ_PRIORITY_MED]);
                        t->priority = MLFQ_PRIORITY_HIGH;
                        t->ticks_remaining = g_MLFQ_Quanta[MLFQ_PRIORITY_HIGH];
                        Queue_PushBack(&g_PerCPU[c].mlfq[MLFQ_PRIORITY_HIGH], t);
                    }
                    while (g_PerCPU[c].mlfq[MLFQ_PRIORITY_LOW].head != NULL)
                    {
                        Task* t = Queue_PopFront(&g_PerCPU[c].mlfq[MLFQ_PRIORITY_LOW]);
                        t->priority = MLFQ_PRIORITY_HIGH;
                        t->ticks_remaining = g_MLFQ_Quanta[MLFQ_PRIORITY_HIGH];
                        Queue_PushBack(&g_PerCPU[c].mlfq[MLFQ_PRIORITY_HIGH], t);
                    }
                    if (g_PerCPU[c].current_task && g_PerCPU[c].current_task->state == TASK_RUNNING)
                    {
                        g_PerCPU[c].current_task->priority = MLFQ_PRIORITY_HIGH;
                        g_PerCPU[c].current_task->ticks_remaining = g_MLFQ_Quanta[MLFQ_PRIORITY_HIGH];
                    }
                    spinlock_release_irqrestore(&g_PerCPU[c].lock, qFlags);
                }
            }

            // Broadcast timer tick IPI to all other online AP cores
            SMP_BroadcastRescheduleIPI();
        }
    }

    // 2. Process current task state on this CPU
    uint8_t rqFlags = spinlock_acquire_irqsave(&my_rq->lock);

    if (curTask != NULL)
    {
        if (curTask->state == TASK_TERMINATED)
        {
            uint8_t rFlags = spinlock_acquire_irqsave(&g_ReaperLock);
            curTask->next = g_TerminatedHead;
            g_TerminatedHead = curTask;
            spinlock_release_irqrestore(&g_ReaperLock, rFlags);
        }
        else if (curTask->state == TASK_SLEEPING)
        {
            uint8_t sFlags = spinlock_acquire_irqsave(&g_SleepLock);
            curTask->next = g_SleepingHead;
            curTask->prev = NULL;
            g_SleepingHead = curTask;
            spinlock_release_irqrestore(&g_SleepLock, sFlags);
        }
        else if (curTask == my_rq->idle_task)
        {
            my_rq->idle_task->state = TASK_READY;
        }
        else if (currentRegs->interrupt == 129)
        {
            // Rule 4b: Voluntary yield
            my_rq->voluntary_yields++;
            curTask->voluntary_yields++;
            curTask->ticks_remaining = g_MLFQ_Quanta[curTask->priority];
            curTask->state = TASK_READY;
            Queue_PushBack(&my_rq->mlfq[curTask->priority], curTask);
        }
        else // Preemptive timer interrupt (32 or IPI 250)
        {
            curTask->total_ticks++;
            if (curTask->ticks_remaining > 0)
                curTask->ticks_remaining--;

            if (curTask->ticks_remaining == 0)
            {
                // Rule 4a: Demote
                my_rq->preemptions++;
                curTask->preemptions++;
                if (curTask->priority < MLFQ_LEVELS - 1)
                {
                    curTask->priority++;
                }
                curTask->ticks_remaining = g_MLFQ_Quanta[curTask->priority];
                curTask->state = TASK_READY;
                Queue_PushBack(&my_rq->mlfq[curTask->priority], curTask);
            }
            else
            {
                // Check higher-priority queue preemption
                bool higherWork = false;
                for (int p = 0; p < curTask->priority; p++)
                {
                    if (my_rq->mlfq[p].head != NULL)
                    {
                        higherWork = true;
                        break;
                    }
                }
                if (higherWork)
                {
                    curTask->state = TASK_READY;
                    Queue_PushBack(&my_rq->mlfq[curTask->priority], curTask);
                }
                else
                {
                    // Continue running current task
                    spinlock_release_irqrestore(&my_rq->lock, rqFlags);
                    return currentRegs;
                }
            }
        }
    }

    // 3. Select next task to run on this CPU
    Task* nextTask = NULL;
    for (int p = 0; p < MLFQ_LEVELS; p++)
    {
        if (my_rq->mlfq[p].head != NULL)
        {
            nextTask = Queue_PopFront(&my_rq->mlfq[p]);
            break;
        }
    }

    // 4. Work Stealing: If local queues are empty, attempt to steal from other CPUs
    if (nextTask == NULL)
    {
        spinlock_release_irqrestore(&my_rq->lock, rqFlags);
        nextTask = Task_StealWork(cpu_id);
        rqFlags = spinlock_acquire_irqsave(&my_rq->lock);
    }

    // 5. If still no task to run, fallback to this core's idle task
    if (nextTask == NULL)
    {
        nextTask = my_rq->idle_task;
    }

    if (nextTask == NULL)
    {
        spinlock_release_irqrestore(&my_rq->lock, rqFlags);
        return currentRegs;
    }

    my_rq->current_task = nextTask;
    nextTask->state = TASK_RUNNING;
    nextTask->assigned_cpu = cpu_id;
    my_rq->total_tasks_run++;

    spinlock_release_irqrestore(&my_rq->lock, rqFlags);

    // 6. Deferred Reaper pass (non-blocking)
    if (spinlock_try_acquire(&g_ReaperLock))
    {
        Task* term = g_TerminatedHead;
        Task* prevTerm = NULL;
        while (term != NULL)
        {
            Task* nextTerm = term->next;

            // Ensure term is NOT executing on ANY CPU and NOT curTask whose stack we are currently on
            bool executing = (term == curTask);
            if (!executing)
            {
                for (uint32_t c = 0; c < SMP_GetOnlineCount(); c++)
                {
                    if (g_PerCPU[c].current_task == term)
                    {
                        executing = true;
                        break;
                    }
                }
            }

            if (!executing && (!term->waitable || term->reapable))
            {
                if (prevTerm)
                    prevTerm->next = nextTerm;
                else
                    g_TerminatedHead = nextTerm;

                vfs_close_task_fds(term);
                GUI_CleanupProcessWindows(term->id);

                if (term->is_user)
                {
                    if (term->page_directory_phys != 0)
                    {
                        for (size_t p = 0; p < term->elf_pages_count; p++)
                        {
                            if (term->elf_pages_phys[p])
                                PMM_FreePage(term->elf_pages_phys[p]);
                        }
                        for (size_t pt = 0; pt < term->elf_pts_count; pt++)
                        {
                            if (term->elf_pts_phys[pt])
                                PMM_FreePage(term->elf_pts_phys[pt]);
                        }
                        PMM_FreePage((void*)term->page_directory_phys);
                    }
                    else
                    {
                        if (term->user_code_virt)
                            VMM_UnmapPage((void*)term->user_code_virt);
                        if (term->user_code_phys)
                            PMM_FreePage((void*)term->user_code_phys);
                        if (term->user_stack_virt)
                            VMM_UnmapPage((void*)term->user_stack_virt);
                        if (term->user_stack_phys)
                            PMM_FreePage((void*)term->user_stack_phys);
                    }
                }

                // Remove from global task registry
                uint8_t regFlags = spinlock_acquire_irqsave(&g_TaskRegistryLock);
                for (int s = 0; s < MAX_SYSTEM_TASKS; s++)
                {
                    if (g_AllTasks[s] == term)
                    {
                        g_AllTasks[s] = NULL;
                        break;
                    }
                }
                spinlock_release_irqrestore(&g_TaskRegistryLock, regFlags);

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
        spinlock_release(&g_ReaperLock);
    }

    // 7. Context switch
    if (nextTask->page_directory_phys != 0)
    {
        if (i686_VMM_GetPageDirectory() != nextTask->page_directory_phys)
        {
            i686_VMM_LoadPageDirectory(nextTask->page_directory_phys);
        }
    }
    else
    {
        uint32_t kpd = (uint32_t)VMM_GetKernelPageDirectory();
        if (kpd && i686_VMM_GetPageDirectory() != kpd)
        {
            i686_VMM_LoadPageDirectory(kpd);
        }
    }

    TSS_SetKernelStackForCPU(cpu_id, nextTask->kernel_stack_top);

    return (Registers*)nextTask->esp;
}

uint32_t Task_GetTicks(void)
{
    return g_SchedulerTicks;
}

Task* Task_GetCurrentForCPU(uint32_t cpu_id)
{
    if (cpu_id < MAX_SMP_CPUS)
        return g_PerCPU[cpu_id].current_task;
    return NULL;
}

Task* Task_GetCurrent(void)
{
    uint32_t cpu_id = SMP_GetCurrentCPUID();
    if (cpu_id < MAX_SMP_CPUS && g_PerCPU[cpu_id].current_task)
        return g_PerCPU[cpu_id].current_task;
    return &g_MainTask;
}

bool Task_IsActive(void)
{
    return g_SchedulerActive;
}

uint32_t Task_GetQueueCount(uint32_t cpu_id, uint8_t priority)
{
    if (cpu_id >= MAX_SMP_CPUS || priority >= MLFQ_LEVELS) return 0;
    return g_PerCPU[cpu_id].mlfq[priority].count;
}

uint32_t Task_GetTotalTasksRun(uint32_t cpu_id)
{
    if (cpu_id >= MAX_SMP_CPUS) return 0;
    return g_PerCPU[cpu_id].total_tasks_run;
}

uint32_t Task_GetTasksStolenFrom(uint32_t cpu_id)
{
    if (cpu_id >= MAX_SMP_CPUS) return 0;
    return g_PerCPU[cpu_id].tasks_stolen_from_others;
}

uint32_t Task_GetTasksStolenBy(uint32_t cpu_id)
{
    if (cpu_id >= MAX_SMP_CPUS) return 0;
    return g_PerCPU[cpu_id].tasks_stolen_by_others;
}

#define STAT_LOG(...) do { printf(__VA_ARGS__); debugf(__VA_ARGS__); } while(0)

void Task_DumpStats(void)
{
    STAT_LOG("\n=== Multi-Core Parallel MLFQ Scheduler Statistics (Tick %u) ===\n", g_SchedulerTicks);
    for (uint32_t c = 0; c < SMP_GetOnlineCount(); c++)
    {
        Task* cur = g_PerCPU[c].current_task;
        STAT_LOG("  --- CPU %u (%s) | Ticks: %u | Runs: %u | Stolen: %u (from) / %u (by) ---\n",
                 c, c == 0 ? "BSP" : "AP",
                 g_PerCPU[c].ticks, g_PerCPU[c].total_tasks_run,
                 g_PerCPU[c].tasks_stolen_from_others, g_PerCPU[c].tasks_stolen_by_others);
        if (cur)
        {
            STAT_LOG("    * PID %u: %s | Q%u | State: %s | Consumed: %u | Yields: %u | Preempts: %u [CURRENT]\n",
                     cur->id, cur->name, cur->priority, Task_StateToString(cur->state),
                     cur->total_ticks, cur->voluntary_yields, cur->preemptions);
        }
        for (int p = 0; p < MLFQ_LEVELS; p++)
        {
            Task* t = g_PerCPU[c].mlfq[p].head;
            while (t)
            {
                if (t != cur)
                {
                    STAT_LOG("      PID %u: %s | Q%u | State: %s | Consumed: %u | Yields: %u | Preempts: %u\n",
                             t->id, t->name, t->priority, Task_StateToString(t->state),
                             t->total_ticks, t->voluntary_yields, t->preemptions);
                }
                t = t->next;
            }
        }
    }
    STAT_LOG("=========================================================================\n\n");
}
