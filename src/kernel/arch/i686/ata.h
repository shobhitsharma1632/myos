#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define ATA_SECTOR_SIZE     512

typedef struct {
    bool        present;
    bool        is_atapi;
    bool        supports_lba48;
    char        model[41];
    char        serial[21];
    uint32_t    total_sectors;  // LBA28 total sector count
    uint32_t    total_mb;       // Total capacity in Megabytes
} ATA_DeviceInfo;

// Public ATA PIO Driver Interface
bool ATA_Initialize(void);
bool ATA_IsAvailable(void);
const ATA_DeviceInfo* ATA_GetDevice(void);

bool ATA_ReadSectors(uint32_t lba, uint8_t count, void* buffer);
bool ATA_WriteSectors(uint32_t lba, uint8_t count, const void* buffer);
bool ATA_FlushCache(void);
