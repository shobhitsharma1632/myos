#include "ata.h"
#include <arch/i686/io.h>
#include <memory.h>
#include <stdio.h>
#include <debug.h>

#define ATA_DATA_PORT           0x1F0
#define ATA_ERROR_PORT          0x1F1
#define ATA_FEATURES_PORT       0x1F1
#define ATA_SECTOR_COUNT_PORT   0x1F2
#define ATA_LBA_LOW_PORT        0x1F3
#define ATA_LBA_MID_PORT        0x1F4
#define ATA_LBA_HIGH_PORT       0x1F5
#define ATA_DRIVE_PORT          0x1F6
#define ATA_STATUS_PORT         0x1F7
#define ATA_COMMAND_PORT        0x1F7
#define ATA_CONTROL_PORT        0x3F6

#define ATA_STATUS_ERR          0x01
#define ATA_STATUS_DRQ          0x08
#define ATA_STATUS_DF           0x20
#define ATA_STATUS_DRDY         0x40
#define ATA_STATUS_BSY          0x80

#define ATA_CMD_READ_PIO        0x20
#define ATA_CMD_WRITE_PIO       0x30
#define ATA_CMD_CACHE_FLUSH     0xE7
#define ATA_CMD_IDENTIFY        0xEC

static ATA_DeviceInfo g_AtaDevice;

static void ATA_Delay400ns(void)
{
    for (int i = 0; i < 4; i++)
    {
        i686_inb(ATA_CONTROL_PORT);
    }
}

static bool ATA_WaitNotBusy(void)
{
    uint32_t timeout = 100000;
    while ((i686_inb(ATA_STATUS_PORT) & ATA_STATUS_BSY) && timeout > 0)
    {
        timeout--;
    }
    return timeout > 0;
}

static bool ATA_WaitDRQ(void)
{
    uint32_t timeout = 100000;
    while (timeout > 0)
    {
        uint8_t status = i686_inb(ATA_STATUS_PORT);
        if (status & (ATA_STATUS_ERR | ATA_STATUS_DF))
            return false;
        if (!(status & ATA_STATUS_BSY) && (status & ATA_STATUS_DRQ))
            return true;
        timeout--;
    }
    return false;
}

bool ATA_Initialize(void)
{
    memset(&g_AtaDevice, 0, sizeof(ATA_DeviceInfo));

    // Disable device interrupts by asserting nIEN
    i686_outb(ATA_CONTROL_PORT, 0x02);

    // Select Primary Master (Drive 0, LBA: 0xA0)
    i686_outb(ATA_DRIVE_PORT, 0xA0);
    ATA_Delay400ns();

    // Zero out sector count and LBA registers
    i686_outb(ATA_SECTOR_COUNT_PORT, 0);
    i686_outb(ATA_LBA_LOW_PORT, 0);
    i686_outb(ATA_LBA_MID_PORT, 0);
    i686_outb(ATA_LBA_HIGH_PORT, 0);

    // Send IDENTIFY command
    i686_outb(ATA_COMMAND_PORT, ATA_CMD_IDENTIFY);
    ATA_Delay400ns();

    uint8_t status = i686_inb(ATA_STATUS_PORT);
    if (status == 0)
    {
        log_warn("ATA", "No drive detected on Primary Master (Status: 0x00)");
        return false;
    }

    if (!ATA_WaitNotBusy())
    {
        log_warn("ATA", "Device busy timeout on IDENTIFY");
        return false;
    }

    // Check cylinder ports to detect ATAPI devices
    uint8_t cl = i686_inb(ATA_LBA_MID_PORT);
    uint8_t ch = i686_inb(ATA_LBA_HIGH_PORT);
    if ((cl == 0x14 && ch == 0xEB) || (cl == 0x69 && ch == 0x96))
    {
        g_AtaDevice.is_atapi = true;
        log_info("ATA", "ATAPI device detected on Primary Master");
        return false;
    }

    if (!ATA_WaitDRQ())
    {
        log_warn("ATA", "IDENTIFY failed to assert DRQ (status: 0x%x)", i686_inb(ATA_STATUS_PORT));
        return false;
    }

    uint16_t identifyData[256];
    for (int i = 0; i < 256; i++)
    {
        identifyData[i] = i686_inw(ATA_DATA_PORT);
    }

    g_AtaDevice.present = true;

    // Serial number (words 10..19, byte swapped)
    for (int i = 0; i < 10; i++)
    {
        g_AtaDevice.serial[i * 2]     = (char)(identifyData[10 + i] >> 8);
        g_AtaDevice.serial[i * 2 + 1] = (char)(identifyData[10 + i] & 0xFF);
    }
    g_AtaDevice.serial[20] = '\0';
    for (int i = 19; i >= 0 && g_AtaDevice.serial[i] == ' '; i--)
        g_AtaDevice.serial[i] = '\0';

    // Model name (words 27..46, byte swapped)
    for (int i = 0; i < 20; i++)
    {
        g_AtaDevice.model[i * 2]     = (char)(identifyData[27 + i] >> 8);
        g_AtaDevice.model[i * 2 + 1] = (char)(identifyData[27 + i] & 0xFF);
    }
    g_AtaDevice.model[40] = '\0';
    for (int i = 39; i >= 0 && g_AtaDevice.model[i] == ' '; i--)
        g_AtaDevice.model[i] = '\0';

    // Total LBA28 sectors (words 60..61)
    g_AtaDevice.total_sectors = ((uint32_t)identifyData[61] << 16) | identifyData[60];
    if (g_AtaDevice.total_sectors == 0)
    {
        // Floppy emulation or default size fallback
        g_AtaDevice.total_sectors = 2880;
    }
    g_AtaDevice.total_mb = (uint32_t)(((uint64_t)g_AtaDevice.total_sectors * ATA_SECTOR_SIZE) / (1024 * 1024));

    g_AtaDevice.supports_lba48 = (identifyData[83] & (1 << 10)) != 0;

    log_info("ATA", "Primary Master detected: '%s' (Serial: '%s', Sectors: %u, Size: %u MB)",
             g_AtaDevice.model, g_AtaDevice.serial, g_AtaDevice.total_sectors, g_AtaDevice.total_mb);

    return true;
}

bool ATA_IsAvailable(void)
{
    return g_AtaDevice.present;
}

const ATA_DeviceInfo* ATA_GetDevice(void)
{
    return &g_AtaDevice;
}

bool ATA_ReadSectors(uint32_t lba, uint8_t count, void* buffer)
{
    if (!g_AtaDevice.present || !buffer)
        return false;

    uint16_t* ptr = (uint16_t*)buffer;

    for (uint8_t s = 0; s < count; s++)
    {
        uint32_t cur_lba = lba + s;

        if (!ATA_WaitNotBusy())
            return false;

        i686_outb(ATA_DRIVE_PORT, 0xE0 | ((cur_lba >> 24) & 0x0F));
        ATA_Delay400ns();

        i686_outb(ATA_SECTOR_COUNT_PORT, 1);
        i686_outb(ATA_LBA_LOW_PORT, (uint8_t)(cur_lba & 0xFF));
        i686_outb(ATA_LBA_MID_PORT, (uint8_t)((cur_lba >> 8) & 0xFF));
        i686_outb(ATA_LBA_HIGH_PORT, (uint8_t)((cur_lba >> 16) & 0xFF));

        i686_outb(ATA_COMMAND_PORT, ATA_CMD_READ_PIO);
        ATA_Delay400ns();

        if (!ATA_WaitDRQ())
            return false;

        for (int w = 0; w < 256; w++)
        {
            *ptr++ = i686_inw(ATA_DATA_PORT);
        }
    }

    return true;
}

bool ATA_WriteSectors(uint32_t lba, uint8_t count, const void* buffer)
{
    if (!g_AtaDevice.present || !buffer)
        return false;

    const uint16_t* ptr = (const uint16_t*)buffer;

    for (uint8_t s = 0; s < count; s++)
    {
        uint32_t cur_lba = lba + s;

        if (!ATA_WaitNotBusy())
            return false;

        i686_outb(ATA_DRIVE_PORT, 0xE0 | ((cur_lba >> 24) & 0x0F));
        ATA_Delay400ns();

        i686_outb(ATA_SECTOR_COUNT_PORT, 1);
        i686_outb(ATA_LBA_LOW_PORT, (uint8_t)(cur_lba & 0xFF));
        i686_outb(ATA_LBA_MID_PORT, (uint8_t)((cur_lba >> 8) & 0xFF));
        i686_outb(ATA_LBA_HIGH_PORT, (uint8_t)((cur_lba >> 16) & 0xFF));

        i686_outb(ATA_COMMAND_PORT, ATA_CMD_WRITE_PIO);
        ATA_Delay400ns();

        if (!ATA_WaitDRQ())
            return false;

        for (int w = 0; w < 256; w++)
        {
            i686_outw(ATA_DATA_PORT, *ptr++);
        }

        ATA_Delay400ns();
        if (!ATA_WaitNotBusy())
            return false;
    }

    ATA_FlushCache();
    return true;
}

bool ATA_FlushCache(void)
{
    if (!g_AtaDevice.present)
        return false;

    if (!ATA_WaitNotBusy())
        return false;

    i686_outb(ATA_COMMAND_PORT, ATA_CMD_CACHE_FLUSH);
    ATA_Delay400ns();

    return ATA_WaitNotBusy();
}
