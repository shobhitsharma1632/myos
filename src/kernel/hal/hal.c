#include "hal.h"
#include <arch/i686/gdt.h>
#include <arch/i686/tss.h>
#include <arch/i686/idt.h>
#include <arch/i686/isr.h>
#include <arch/i686/irq.h>
#include <arch/i686/vga_text.h>
#include <arch/i686/keyboard.h>
#include <hal/syscall.h>

void HAL_Initialize()
{
    VGA_clrscr();
    i686_GDT_Initialize();
    TSS_Initialize(i686_GDT_DATA_SEGMENT);
    i686_IDT_Initialize();
    i686_ISR_Initialize();
    i686_IRQ_Initialize();
    Syscall_Initialize();
    i686_Keyboard_Initialize();
}