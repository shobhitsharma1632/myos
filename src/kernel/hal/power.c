#include "power.h"
#include <arch/i686/io.h>
#include <debug.h>

#define MODULE "POWER"

static volatile bool g_PowerSleeping = false;
static volatile bool g_PowerHibernating = false;

void Power_Initialize(void)
{
    g_PowerSleeping = false;
    g_PowerHibernating = false;
}

bool Power_IsSleeping(void)
{
    return g_PowerSleeping;
}

bool Power_IsHibernating(void)
{
    return g_PowerHibernating;
}

void Power_Wakeup(void)
{
    if (g_PowerSleeping || g_PowerHibernating)
    {
        g_PowerSleeping = false;
        g_PowerHibernating = false;
        log_info(MODULE, "System woke up from suspend/hibernate.");
    }
}

void Power_Sleep(void)
{
    log_info(MODULE, "Entering low-power sleep state (S3 Standby)...");
    g_PowerSleeping = true;
    g_PowerHibernating = false;
}

void Power_Hibernate(void)
{
    log_info(MODULE, "Entering system hibernation (S4 Suspend to Disk)...");
    g_PowerHibernating = true;
    g_PowerSleeping = false;
}

void Power_Reboot(void)
{
    log_info(MODULE, "Rebooting myOS via 8042 controller / PCI reset...");

    // 1. Pulse 8042 PS/2 keyboard controller reset line
    for (int i = 0; i < 10000; i++)
    {
        if ((i686_inb(0x64) & 0x02) == 0) break;
    }
    i686_outb(0x64, 0xFE);

    // 2. PCI / ACPI reset control register
    for (volatile int j = 0; j < 50000; j++);
    i686_outb(0xCF9, 0x06);

    // 3. Fallback: CPU Triple Fault
    for (volatile int j = 0; j < 50000; j++);
    struct { uint16_t limit; uint32_t base; } __attribute__((packed)) null_idt = { 0, 0 };
    __asm__ volatile ("lidt %0; int $3" : : "m"(null_idt));

    // Fallback halt
    __asm__ volatile ("cli");
    while (1) { __asm__ volatile ("hlt"); }
}

void Power_Shutdown(void)
{
    log_info(MODULE, "Shutting down myOS and powering off machine...");

    // 1. QEMU ACPI poweroff port
    i686_outw(0x604, 0x2000);

    // 2. Bochs / older QEMU shutdown port
    i686_outw(0xB004, 0x2000);

    // 3. VirtualBox shutdown port
    i686_outw(0x4004, 0x3400);

    // 4. APM shutdown
    i686_outw(0x8900, 0x5307);

    // If machine hasn't shut down, halt CPU
    __asm__ volatile ("cli");
    while (1) { __asm__ volatile ("hlt"); }
}
