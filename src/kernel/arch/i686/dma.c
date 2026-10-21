#include "dma.h"
#include <arch/i686/io.h>
#include <hal/pmm.h>
#include <debug.h>
#include "memory.h"
#include <string.h>

#define MODULE "DMA"

static void*    g_DMABufferVirt = NULL;
static uint32_t g_DMABufferPhys = 0;

static const uint8_t g_PagePorts[8] = {
    0x87, 0x83, 0x81, 0x82, 0x8F, 0x8B, 0x89, 0x8A
};

static const uint8_t g_AddrPorts[8] = {
    0x00, 0x02, 0x04, 0x06, 0xC0, 0xC4, 0xC8, 0xCC
};

static const uint8_t g_CountPorts[8] = {
    0x01, 0x03, 0x05, 0x07, 0xC2, 0xC6, 0xCA, 0xCE
};

void DMA_Initialize(void)
{
    // Allocate contiguous physical memory below 16 MB that does not cross a 64 KiB boundary
    // Allocate 32 KiB (8 pages) to ensure we can find a 16 KiB window within one 64 KiB block.
    void* block = PMM_AllocatePages(8);
    if (!block)
    {
        log_crit(MODULE, "Failed to allocate physical pages for ISA DMA buffer!");
        return;
    }

    uint32_t phys = (uint32_t)block;
    // Check if the first 16 KiB crosses a 64 KiB boundary
    if ((phys & 0xFFFF) + DMA_BUFFER_SIZE > 0x10000)
    {
        // Align up to next 64 KiB boundary within the allocated 32 KiB
        uint32_t aligned_phys = (phys + 0xFFFF) & ~0xFFFF;
        g_DMABufferPhys = aligned_phys;
        g_DMABufferVirt = (void*)aligned_phys;
    }
    else
    {
        g_DMABufferPhys = phys;
        g_DMABufferVirt = (void*)phys;
    }

    memset(g_DMABufferVirt, 0, DMA_BUFFER_SIZE);

    log_info(MODULE, "Initialized ISA DMA Controller: 16 KiB buffer at phys 0x%x (virt 0x%x)",
             g_DMABufferPhys, (uint32_t)g_DMABufferVirt);
}

void DMA_MaskChannel(uint8_t channel)
{
    if (channel < 4)
    {
        i686_outb(0x0A, 0x04 | channel);
    }
    else
    {
        i686_outb(0xD4, 0x04 | (channel - 4));
    }
}

void DMA_UnmaskChannel(uint8_t channel)
{
    if (channel < 4)
    {
        i686_outb(0x0A, channel);
    }
    else
    {
        i686_outb(0xD4, channel - 4);
    }
}

bool DMA_SetupChannel(uint8_t channel, uint32_t phys_addr, uint32_t length, uint8_t mode)
{
    if (channel > 7 || length == 0) return false;

    // Verify 64 KiB boundary rule
    if ((phys_addr & 0xFFFF) + length > 0x10000)
    {
        log_err(MODULE, "DMA transfer crosses 64 KiB physical boundary (0x%x + %u)!",
                phys_addr, length);
        return false;
    }

    DMA_MaskChannel(channel);

    if (channel < 4)
    {
        // 8-bit Channels (0–3)
        // Set mode: mode | channel
        i686_outb(0x0B, (mode & ~0x03) | channel);

        // Clear byte pointer flip-flop
        i686_outb(0x0C, 0xFF);

        // Output Page Register (bits 16..23)
        i686_outb(g_PagePorts[channel], (uint8_t)(phys_addr >> 16));

        // Output Base Address (low byte, then high byte)
        i686_outb(g_AddrPorts[channel], (uint8_t)(phys_addr & 0xFF));
        i686_outb(g_AddrPorts[channel], (uint8_t)((phys_addr >> 8) & 0xFF));

        // Output Transfer Count: count = length - 1
        uint16_t count = (uint16_t)(length - 1);
        i686_outb(g_CountPorts[channel], (uint8_t)(count & 0xFF));
        i686_outb(g_CountPorts[channel], (uint8_t)((count >> 8) & 0xFF));
    }
    else
    {
        // 16-bit Channels (4–7)
        uint8_t ch = channel - 4;

        // Set mode: mode | ch
        i686_outb(0xD6, (mode & ~0x03) | ch);

        // Clear byte pointer flip-flop
        i686_outb(0xD8, 0xFF);

        // Output Page Register (bits 16..23)
        i686_outb(g_PagePorts[channel], (uint8_t)(phys_addr >> 16));

        // Output Base Address in 16-bit words: (phys_addr / 2) & 0xFFFF
        uint16_t word_addr = (uint16_t)((phys_addr >> 1) & 0xFFFF);
        i686_outb(g_AddrPorts[channel], (uint8_t)(word_addr & 0xFF));
        i686_outb(g_AddrPorts[channel], (uint8_t)((word_addr >> 8) & 0xFF));

        // Output Transfer Count in 16-bit words: (length / 2) - 1
        uint16_t word_count = (uint16_t)((length >> 1) - 1);
        i686_outb(g_CountPorts[channel], (uint8_t)(word_count & 0xFF));
        i686_outb(g_CountPorts[channel], (uint8_t)((word_count >> 8) & 0xFF));
    }

    DMA_UnmaskChannel(channel);
    return true;
}

void* DMA_GetBuffer(void)
{
    return g_DMABufferVirt;
}

uint32_t DMA_GetBufferPhys(void)
{
    return g_DMABufferPhys;
}
