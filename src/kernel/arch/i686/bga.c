#include "bga.h"
#include "pci.h"
#include <arch/i686/io.h>
#include <hal/vmm.h>
#include <hal/pmm.h>
#include <debug.h>
#include <stdbool.h>
#include <stddef.h>

#define MODULE "BGA"

static uint32_t g_Width = 0;
static uint32_t g_Height = 0;
static uint32_t g_Bpp = 0;
static uint32_t g_Pitch = 0;
static void*    g_Framebuffer = NULL;
static uint32_t g_PhysFramebuffer = 0;

static void BGA_WriteRegister(uint16_t index, uint16_t value)
{
    i686_outw(BGA_INDEX_PORT, index);
    i686_outw(BGA_DATA_PORT, value);
}

static uint16_t BGA_ReadRegister(uint16_t index)
{
    i686_outw(BGA_INDEX_PORT, index);
    return i686_inw(BGA_DATA_PORT);
}

bool BGA_IsAvailable(void)
{
    uint16_t version = BGA_ReadRegister(VBE_DISPI_INDEX_ID);
    return (version >= 0xB0C0 && version <= 0xB0C5);
}

uint16_t BGA_GetVersion(void)
{
    return BGA_ReadRegister(VBE_DISPI_INDEX_ID);
}

uint32_t BGA_GetPhysicalFramebuffer(void)
{
    if (g_PhysFramebuffer != 0)
        return g_PhysFramebuffer;

    uint8_t bus, slot, func;
    // QEMU BGA device PCI Vendor: 0x1234, Device: 0x1111
    if (pci_find_device(0x1234, 0x1111, &bus, &slot, &func))
    {
        uint32_t bar0 = pci_get_bar(bus, slot, func, 0);
        if (bar0 != 0)
        {
            g_PhysFramebuffer = bar0;
            log_info(MODULE, "Found BGA PCI device at %02x:%02x.%d, LFB BAR0: 0x%x",
                     bus, slot, func, g_PhysFramebuffer);
            return g_PhysFramebuffer;
        }
    }

    // Standard fallback for QEMU / Bochs
    g_PhysFramebuffer = BGA_DEFAULT_LFB_PHYS;
    log_info(MODULE, "BGA PCI device not explicitly mapped; using default LFB 0x%x", g_PhysFramebuffer);
    return g_PhysFramebuffer;
}

bool BGA_SetVideoMode(uint32_t width, uint32_t height, uint32_t bpp)
{
    if (!BGA_IsAvailable())
    {
        log_err(MODULE, "Bochs Graphics Adapter not detected!");
        return false;
    }

    log_info(MODULE, "Setting video mode: %ux%u @ %ubpp...", width, height, bpp);

    BGA_WriteRegister(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    BGA_WriteRegister(VBE_DISPI_INDEX_XRES, (uint16_t)width);
    BGA_WriteRegister(VBE_DISPI_INDEX_YRES, (uint16_t)height);
    BGA_WriteRegister(VBE_DISPI_INDEX_BPP, (uint16_t)bpp);
    BGA_WriteRegister(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);

    g_Width = width;
    g_Height = height;
    g_Bpp = bpp;
    g_Pitch = width * (bpp / 8);

    uint32_t physFb = BGA_GetPhysicalFramebuffer();
    uint32_t totalBytes = g_Pitch * height;

    // Map physical framebuffer into virtual address space
    // We map it identity at physFb (e.g. 0xE0000000)
    for (uint32_t offset = 0; offset < totalBytes; offset += 4096)
    {
        void* virt = (void*)(physFb + offset);
        void* phys = (void*)(physFb + offset);
        if (!VMM_MapPage(virt, phys, PAGE_PRESENT | PAGE_WRITE))
        {
            log_err(MODULE, "Failed to map framebuffer page at virt 0x%x -> phys 0x%x",
                    (uint32_t)virt, (uint32_t)phys);
            return false;
        }
    }

    g_Framebuffer = (void*)physFb;
    log_info(MODULE, "BGA Mode %ux%u @ %ubpp active! Framebuffer mapped at 0x%x (Size: %u KB)",
             width, height, bpp, (uint32_t)g_Framebuffer, totalBytes / 1024);

    return true;
}

void* BGA_GetFramebuffer(void)
{
    return g_Framebuffer;
}

uint32_t BGA_GetWidth(void)
{
    return g_Width;
}

uint32_t BGA_GetHeight(void)
{
    return g_Height;
}

uint32_t BGA_GetBpp(void)
{
    return g_Bpp;
}

uint32_t BGA_GetPitch(void)
{
    return g_Pitch;
}
