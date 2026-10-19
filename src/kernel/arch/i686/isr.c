#include "isr.h"
#include "idt.h"
#include "gdt.h"
#include "io.h"
#include <stdio.h>
#include <stddef.h>
#include <hal/task.h>

ISRHandler g_ISRHandlers[256];

static const char* const g_Exceptions[] = {
    "Divide by zero error",
    "Debug",
    "Non-maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "",
    "x87 FPU Floating-Point Error",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point Exception",
    "Virtualization Exception",
    "Control Protection Exception",
    "",
    "",
    "",
    "",
    "",
    "",
    "Hypervisor Injection Exception",
    "VMM Communication Exception",
    "Security Exception",
    ""
};

#include <hal/syscall.h>
#include <debug.h>

void i686_ISR_InitializeGates();

void i686_ISR_Initialize()
{
    i686_ISR_InitializeGates();
    for (int i = 0; i < 256; i++)
        i686_IDT_EnableGate(i);
}

Registers* __attribute__((cdecl)) i686_ISR_Handler(Registers* regs)
{
    if (g_ISRHandlers[regs->interrupt] != NULL)
        g_ISRHandlers[regs->interrupt](regs);
    else if (regs->interrupt >= 32)
    {
        if (regs->interrupt != 129 && regs->interrupt != 128)
            printf("Unhandled interrupt %d!\n", regs->interrupt);
    }
    else
    {
        // Handle User Mode Exceptions (Ring 3 Fault Containment)
        if ((regs->cs & 3) == 3)
        {
            Task* cur = Task_GetCurrent();
            const char* excName = (regs->interrupt < 32 && g_Exceptions[regs->interrupt][0]) ?
                                  g_Exceptions[regs->interrupt] : "Exception";
            log_warn("ISR", "User Process '%s' (PID %u) triggered exception %d (%s) at EIP 0x%x (Error 0x%x)!",
                     cur ? cur->name : "unknown",
                     cur ? cur->id : 0,
                     regs->interrupt, excName, regs->eip, regs->error);
            printf("\n[USER FAULT] Process '%s' (PID %u) killed by exception %d (%s) at EIP 0x%x (Error 0x%x)\n",
                   cur ? cur->name : "unknown",
                   cur ? cur->id : 0,
                   regs->interrupt, excName, regs->eip, regs->error);

            Task_Exit();
            return Task_Schedule(regs);
        }

        printf("Unhandled exception %d %s\n", regs->interrupt, g_Exceptions[regs->interrupt]);

        printf("  eax=%x ebx=%x ecx=%x edx=%x esi=%x edi=%x\n", 
                regs->eax, regs->ebx, regs->ecx, regs->edx, regs->esi, regs->edi);
        
        printf("  esp=%x ebp=%x eip=%x eflags=%x cs=%x ds=%x ss=%x\n",
                regs->esp, regs->ebp, regs->eip, regs->eflags, regs->cs, regs->ds, regs->ss);

        printf("  interrupt=%x errorcode=%x\n", regs->interrupt, regs->error);

        printf("KERNEL PANIC!\n");
        i686_Panic();
    }

    if (Task_IsActive() && (regs->interrupt == 32 || regs->interrupt == 129 || Syscall_TakeYieldRequest()))
    {
        return Task_Schedule(regs);
    }

    return regs;
}

void i686_ISR_RegisterHandler(int interrupt, ISRHandler handler)
{
    g_ISRHandlers[interrupt] = handler;
    i686_IDT_EnableGate(interrupt);
}

