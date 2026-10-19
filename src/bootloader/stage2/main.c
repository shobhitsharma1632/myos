#include <stdint.h>
#include <stddef.h>
#include "stdio.h"
#include "x86.h"
#include "disk.h"
#include "mbr.h"
#include "fat.h"
#include "memdefs.h"
#include "memory.h"
#include "memdetect.h"
#include "elf.h"
#include <boot/bootparams.h>

BootParams g_BootParams;

typedef void (*KernelStart)(BootParams* bootParams);

void __attribute__((cdecl)) start(uint16_t bootDrive, void* partition)
{
    clrscr();
    printf("Starting Stage 2...\r\n");

    DISK disk;
    printf("Initializing disk...\r\n");
    if (!DISK_Initialize(&disk, bootDrive))
    {
        printf("Disk init error\r\n");
        goto end;
    }
    printf("Disk initialized\r\n");

    Partition part;
    MBR_DetectPartition(&part, &disk, partition);

    // Detect physical memory via BIOS E820
    printf("Detecting memory...\r\n");
    g_BootParams.BootDevice = (uint8_t)bootDrive;
    Memory_Detect(&g_BootParams.Memory);
    printf("Detected %d memory regions\r\n", g_BootParams.Memory.RegionCount);

    printf("Initializing FAT...\r\n");
    if (!FAT_Initialize(&part))
    {
        printf("FAT init error\r\n");
        goto end;
    }
    printf("FAT initialized\r\n");

    // Load kernel ELF
    printf("Loading kernel ELF...\r\n");
    KernelStart kernelEntry = NULL;
    if (!ELF_Read(&part, "/boot/kernel.elf", (void**)&kernelEntry))
    {
        printf("ELF read failed, booting halted!\r\n");
        goto end;
    }
    printf("Kernel loaded at %p\r\n", kernelEntry);

    // Execute kernel passing boot parameters
    printf("Executing kernel...\r\n");
    __asm__ volatile("cli");
    kernelEntry(&g_BootParams);

end:
    printf("Halted.\r\n");
    for (;;);
}