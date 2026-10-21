#include "task.h"
#include "elf_loader.h"
#include <hal/vfs.h>
#include <hal/gui.h>
#include <hal/heap.h>
#include <hal/vmm.h>
#include <hal/pmm.h>
#include <arch/i686/io.h>
#include <arch/i686/irq.h>
#include <arch/i686/gdt.h>
#include <arch/i686/tss.h>
#include <memory.h>
#include <string.h>
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
static Task*        g_SleepingHead = NULL;
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
    vfs_init_task_fds(&g_MainTask);

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
    task->is_user = false;
    task->kernel_stack_top = (uint32_t)stack + TASK_STACK_SIZE;
    vfs_init_task_fds(task);

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

Task* Task_CreateUser(const char* name, const void* code, size_t codeSize)
{
    Task* task = (Task*)kmalloc(sizeof(Task));
    if (!task)
    {
        log_err(MODULE, "Failed to allocate TCB for user task '%s'!", name ? name : "unnamed");
        return NULL;
    }
    memset(task, 0, sizeof(Task));

    // Allocate 4 KiB kernel stack for privilege transitions (Ring 3 -> Ring 0)
    void* kstack = kmalloc(TASK_STACK_SIZE);
    if (!kstack)
    {
        log_err(MODULE, "Failed to allocate kernel stack for user task '%s'!", name ? name : "unnamed");
        kfree(task);
        return NULL;
    }
    memset(kstack, 0, TASK_STACK_SIZE);

    task->magic = TASK_MAGIC;
    task->id = g_NextPID++;
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

    // Insert task into MLFQ Queue 0
    uint8_t ints = i686_DisableInterrupts();
    Queue_PushBack(&g_MLFQ[MLFQ_PRIORITY_HIGH], task);
    if (ints) i686_EnableInterrupts();

    log_info(MODULE, "Created User Process '%s' (PID %u): Code @ 0x%x, Stack @ 0x%x, KStack @ 0x%x",
             task->name, task->id, task->user_code_virt, regs->esp, task->kernel_stack_top);

    return task;
}

void Task_Yield(void)
{
    __asm__ volatile("int $0x81");
}

void Task_ExitWithCode(int32_t code)
{
    // Atomically transition task to TERMINATED state and enqueue to reaper list
    i686_DisableInterrupts();

    Task* current = g_CurrentTask;
    if (current)
    {
        vfs_close_task_fds(current);
        current->exit_code = code;
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

void Task_Exit(void)
{
    Task_ExitWithCode(0);
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
                vfs_close_task_fds(t);
                t->state = TASK_TERMINATED;
                t->reapable = true;
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

bool Task_SendSignal(uint32_t pid, int sig)
{
    if (sig <= 0 || sig >= NSIG)
        return false;

    uint8_t ints = i686_DisableInterrupts();
    Task* t = Task_Find(pid);
    if (!t || t->state == TASK_TERMINATED)
    {
        if (ints) i686_EnableInterrupts();
        return false;
    }

    // SIGKILL cannot be caught or ignored
    if (sig == SIGKILL)
    {
        if (ints) i686_EnableInterrupts();
        return Task_Kill(pid);
    }

    t->pending_signals |= (1 << sig);

    // If task was sleeping (e.g. waiting on pause() or sleep()), wake it up so signal is processed
    if (t->state == TASK_SLEEPING)
    {
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

        t->state = TASK_READY;
        t->next = NULL;
        t->prev = NULL;
        Queue_PushBack(&g_MLFQ[t->priority], t);
    }

    if (ints) i686_EnableInterrupts();
    return true;
}

Task* Task_Find(uint32_t pid)
{
    if (g_CurrentTask && g_CurrentTask->id == pid)
        return g_CurrentTask;

    for (int p = 0; p < MLFQ_LEVELS; p++)
    {
        Task* t = g_MLFQ[p].head;
        while (t)
        {
            if (t->id == pid)
                return t;
            t = t->next;
        }
    }

    Task* s = g_SleepingHead;
    while (s)
    {
        if (s->id == pid)
            return s;
        s = s->next;
    }

    Task* term = g_TerminatedHead;
    while (term)
    {
        if (term->id == pid)
            return term;
        term = term->next;
    }

    return NULL;
}

int32_t Task_WaitPID(uint32_t pid, int32_t* outExitCode)
{
    while (true)
    {
        Task* t = Task_Find(pid);
        if (!t)
        {
            // Process does not exist or has already been reaped
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
    task->id = g_NextPID++;
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
    vfs_clone_task_fds(g_CurrentTask, task);
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

    // Copy argument strings to top of user stack
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
    // [esp + 8] = argv pointer (user virtual address)
    // [esp + 4] = argc
    // [esp + 0] = dummy return address
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

    // Insert task into MLFQ Queue 0
    uint8_t ints = i686_DisableInterrupts();
    Queue_PushBack(&g_MLFQ[MLFQ_PRIORITY_HIGH], task);
    if (ints) i686_EnableInterrupts();

    log_info(MODULE, "Created ELF User Process '%s' (PID %u): Entry @ 0x%x, Stack @ 0x%x, PD @ 0x%x",
             task->name, task->id, regs->eip, regs->esp, task->page_directory_phys);

    return task;
}

static void Task_UpdateAlarms(void)
{
    for (int p = 0; p < MLFQ_LEVELS; p++)
    {
        Task* t = g_MLFQ[p].head;
        while (t)
        {
            if (t != g_CurrentTask && t->alarm_ticks > 0)
            {
                t->alarm_ticks--;
                if (t->alarm_ticks == 0)
                {
                    Task_SendSignal(t->id, SIGALRM);
                }
            }
            t = t->next;
        }
    }

    Task* s = g_SleepingHead;
    while (s)
    {
        if (s != g_CurrentTask && s->alarm_ticks > 0)
        {
            s->alarm_ticks--;
            if (s->alarm_ticks == 0)
            {
                Task_SendSignal(s->id, SIGALRM);
            }
        }
        s = s->next;
    }

    if (g_CurrentTask && g_CurrentTask->alarm_ticks > 0)
    {
        g_CurrentTask->alarm_ticks--;
        if (g_CurrentTask->alarm_ticks == 0)
        {
            Task_SendSignal(g_CurrentTask->id, SIGALRM);
        }
    }
}

void Task_DeliverSignals(Registers* regs)
{
    if (!regs) return;

    // Only deliver signals when returning to Ring 3 User Mode
    if ((regs->cs & 3) != 3)
        return;

    Task* task = g_CurrentTask;
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

    // Clear pending signal bit
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
    if (!g_SchedulerActive || g_CurrentTask == NULL)
        return currentRegs;

    // 1. Save execution state of current task
    g_CurrentTask->esp = (uint32_t)currentRegs;
    g_SchedulerTicks++;
    Task_UpdateAlarms();

    // Poll network hardware for incoming packets
    extern void Net_Poll(void);
    Net_Poll();

    // Wake up any sleeping tasks whose timer expired
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
            Queue_PushBack(&g_MLFQ[sleepTask->priority], sleepTask);
        }
        else
        {
            prevSleep = sleepTask;
        }
        sleepTask = nextSleep;
    }

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

        log_info(MODULE, "MLFQ Boost: tick %u, cur='%s' (PID %u, state %d)",
                 g_SchedulerTicks, g_CurrentTask ? g_CurrentTask->name : "none",
                 g_CurrentTask ? g_CurrentTask->id : 0, g_CurrentTask ? g_CurrentTask->state : -1);
    }

    // 3. Update current task state according to MLFQ Rules 4a & 4b
    bool needReschedule = false;

    if (g_CurrentTask->state == TASK_TERMINATED)
    {
        // Task terminated; already on reaper list, do not re-enqueue
        needReschedule = true;
    }
    else if (g_CurrentTask->state == TASK_SLEEPING)
    {
        // Task requested sleep; enqueue onto sleeping list
        g_CurrentTask->next = g_SleepingHead;
        g_CurrentTask->prev = NULL;
        g_SleepingHead = g_CurrentTask;
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
        if (term != executingTask && (!term->waitable || term->reapable))
        {
            if (prevTerm)
                prevTerm->next = nextTerm;
            else
                g_TerminatedHead = nextTerm;

            log_info(MODULE, "Reaper: Safely freed stack (0x%x) and TCB for terminated %s task '%s' (PID %u)",
                     term->stack_base, term->is_user ? "User" : "Kernel", term->name, term->id);

            vfs_close_task_fds(term);
            GUI_CleanupProcessWindows(term->id);

            // Free user-mode virtual memory mappings and physical frames
            if (term->is_user)
            {
                if (term->page_directory_phys != 0)
                {
                    // Free tracked ELF physical pages (code, data, stack)
                    for (size_t p = 0; p < term->elf_pages_count; p++)
                    {
                        if (term->elf_pages_phys[p])
                            PMM_FreePage(term->elf_pages_phys[p]);
                    }
                    // Free tracked Page Tables
                    for (size_t pt = 0; pt < term->elf_pts_count; pt++)
                    {
                        if (term->elf_pts_phys[pt])
                            PMM_FreePage(term->elf_pts_phys[pt]);
                    }
                    // Free Page Directory
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

    // Switch Page Directory if task has its own address space
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

    // Update TSS kernel stack pointer for privilege transitions (Ring 3 -> Ring 0)
    TSS_SetKernelStack(nextTask->kernel_stack_top);

    return (Registers*)nextTask->esp;
}

uint32_t Task_GetTicks(void)
{
    return g_SchedulerTicks;
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
