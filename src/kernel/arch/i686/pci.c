#include "pci.h"
#include <arch/i686/io.h>

uint32_t pci_read_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
    uint32_t address = (uint32_t)((((uint32_t)bus) << 16) |
                                  (((uint32_t)slot) << 11) |
                                  (((uint32_t)func) << 8) |
                                  (offset & 0xFC) |
                                  0x80000000);
    i686_outd(PCI_CONFIG_ADDRESS, address);
    return i686_ind(PCI_CONFIG_DATA);
}

void pci_write_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value)
{
    uint32_t address = (uint32_t)((((uint32_t)bus) << 16) |
                                  (((uint32_t)slot) << 11) |
                                  (((uint32_t)func) << 8) |
                                  (offset & 0xFC) |
                                  0x80000000);
    i686_outd(PCI_CONFIG_ADDRESS, address);
    i686_outd(PCI_CONFIG_DATA, value);
}

uint16_t pci_read_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
    uint32_t val = pci_read_dword(bus, slot, func, offset);
    return (uint16_t)((val >> ((offset & 2) * 8)) & 0xFFFF);
}

uint8_t pci_read_byte(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
    uint32_t val = pci_read_dword(bus, slot, func, offset);
    return (uint8_t)((val >> ((offset & 3) * 8)) & 0xFF);
}

bool pci_find_device(uint16_t vendorId, uint16_t deviceId, uint8_t* outBus, uint8_t* outSlot, uint8_t* outFunc)
{
    for (uint16_t bus = 0; bus < 256; bus++)
    {
        for (uint8_t slot = 0; slot < 32; slot++)
        {
            uint16_t vendor = pci_read_word((uint8_t)bus, slot, 0, PCI_VENDOR_ID);
            if (vendor == 0xFFFF)
                continue; // Device not present

            uint8_t headerType = pci_read_byte((uint8_t)bus, slot, 0, PCI_HEADER_TYPE);
            uint8_t numFuncs = (headerType & 0x80) ? 8 : 1;

            for (uint8_t func = 0; func < numFuncs; func++)
            {
                uint16_t v = pci_read_word((uint8_t)bus, slot, func, PCI_VENDOR_ID);
                if (v == 0xFFFF)
                    continue;

                uint16_t d = pci_read_word((uint8_t)bus, slot, func, PCI_DEVICE_ID);
                if (v == vendorId && d == deviceId)
                {
                    if (outBus) *outBus = (uint8_t)bus;
                    if (outSlot) *outSlot = slot;
                    if (outFunc) *outFunc = func;
                    return true;
                }
            }
        }
    }
    return false;
}

uint32_t pci_get_bar(uint8_t bus, uint8_t slot, uint8_t func, uint8_t barIndex)
{
    if (barIndex > 5) return 0;
    uint8_t offset = PCI_BAR0 + (barIndex * 4);
    uint32_t bar = pci_read_dword(bus, slot, func, offset);

    // Memory BAR (bit 0 == 0) vs I/O BAR (bit 0 == 1)
    if ((bar & 1) == 0)
    {
        return bar & 0xFFFFFFF0;
    }
    return bar & 0xFFFFFFFC;
}
