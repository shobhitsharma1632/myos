#include "fat.h"
#include <arch/i686/ata.h>
#include <memory.h>
#include <stdio.h>
#include <string.h>
#include <debug.h>

typedef struct {
    uint8_t  boot_jump[3];
    uint8_t  oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  fat_count;
    uint16_t root_entry_count;
    uint16_t total_sectors_short;
    uint8_t  media_type;
    uint16_t sectors_per_fat;
    uint16_t sectors_per_track;
    uint16_t head_count;
    uint32_t hidden_sectors;
    uint32_t total_sectors_large;
    // Extended BPB
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_signature;
    uint32_t volume_id;
    uint8_t  volume_label[11];
    uint8_t  fs_type[8];
} __attribute__((packed)) FAT_BPB;

typedef struct {
    uint8_t  name[11];
    uint8_t  attributes;
    uint8_t  nt_reserved;
    uint8_t  created_time_tenths;
    uint16_t created_time;
    uint16_t created_date;
    uint16_t accessed_date;
    uint16_t first_cluster_high;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t first_cluster_low;
    uint32_t file_size;
} __attribute__((packed)) FAT_DirEntry;

#define MAX_OPEN_FILES      16
#define FAT_CACHE_SECTORS   32

struct FAT_Handle {
    bool     used;
    bool     is_dir;
    bool     write_mode;
    char     path[128];
    uint32_t first_cluster;
    uint32_t current_cluster;
    uint32_t current_sector_in_cluster;
    uint32_t file_size;
    uint32_t position;
    uint32_t dir_sector_lba;
    uint32_t dir_entry_index;
    uint8_t  sector_buffer[512];
    uint32_t buffer_lba;
    bool     buffer_dirty;
};

static FAT_BPB     g_Bpb;
static bool        g_Mounted = false;
static uint8_t     g_FatType = 12; // 12 or 16
static uint32_t    g_RootDirLba = 0;
static uint32_t    g_RootDirSectors = 0;
static uint32_t    g_DataSectionLba = 0;
static uint32_t    g_TotalClusters = 0;
static uint8_t     g_FatCache[512 * FAT_CACHE_SECTORS];
static bool        g_FatCacheDirty = false;

static FAT_Handle  g_Handles[MAX_OPEN_FILES];

static inline uint32_t FAT_ClusterToLba(uint32_t cluster)
{
    if (cluster < 2) return 0;
    return g_DataSectionLba + (cluster - 2) * g_Bpb.sectors_per_cluster;
}

static char FAT_ToUpper(char c)
{
    if (c >= 'a' && c <= 'z') return c - ('a' - 'A');
    return c;
}

void FAT_FormatFileName(const uint8_t fatName[11], char* outName)
{
    int outIdx = 0;
    int nameEnd = 7;
    while (nameEnd >= 0 && fatName[nameEnd] == ' ')
        nameEnd--;

    for (int i = 0; i <= nameEnd; i++)
    {
        outName[outIdx++] = fatName[i];
    }

    int extEnd = 10;
    while (extEnd >= 8 && fatName[extEnd] == ' ')
        extEnd--;

    if (extEnd >= 8)
    {
        outName[outIdx++] = '.';
        for (int i = 8; i <= extEnd; i++)
        {
            outName[outIdx++] = fatName[i];
        }
    }
    outName[outIdx] = '\0';
}

void FAT_ToDosFileName(const char* inName, uint8_t dosName[11])
{
    memset(dosName, ' ', 11);

    // Extract basename from path
    const char* base = inName;
    for (const char* p = inName; *p; p++)
    {
        if (*p == '/' || *p == '\\')
            base = p + 1;
    }

    int i = 0;
    int dosIdx = 0;
    while (base[i] && base[i] != '.' && dosIdx < 8)
    {
        dosName[dosIdx++] = FAT_ToUpper(base[i++]);
    }

    while (base[i] && base[i] != '.')
        i++;

    if (base[i] == '.')
    {
        i++;
        dosIdx = 8;
        while (base[i] && dosIdx < 11)
        {
            dosName[dosIdx++] = FAT_ToUpper(base[i++]);
        }
    }
}

static uint32_t FAT_GetNextCluster(uint32_t cluster)
{
    if (cluster >= 0x0FF8) return 0x0FFFFFFF;

    uint32_t fatOffset;
    if (g_FatType == 12)
    {
        fatOffset = cluster + (cluster / 2);
    }
    else
    {
        fatOffset = cluster * 2;
    }

    if (fatOffset + 2 > sizeof(g_FatCache))
        return 0x0FFFFFFF;

    uint32_t next = 0;
    if (g_FatType == 12)
    {
        uint16_t val = *(uint16_t*)&g_FatCache[fatOffset];
        if (cluster & 1)
            next = val >> 4;
        else
            next = val & 0x0FFF;

        if (next >= 0x0FF8)
            next = 0x0FFFFFFF;
    }
    else
    {
        next = *(uint16_t*)&g_FatCache[fatOffset];
        if (next >= 0xFFF8)
            next = 0x0FFFFFFF;
    }

    return next;
}

static void FAT_SetCluster(uint32_t cluster, uint32_t value)
{
    if (cluster < 2 || cluster >= g_TotalClusters + 2) return;

    if (g_FatType == 12)
    {
        uint32_t fatOffset = cluster + (cluster / 2);
        if (fatOffset + 2 > sizeof(g_FatCache)) return;

        uint16_t val = *(uint16_t*)&g_FatCache[fatOffset];
        if (cluster & 1)
        {
            val = (val & 0x000F) | ((value & 0x0FFF) << 4);
        }
        else
        {
            val = (val & 0xF000) | (value & 0x0FFF);
        }
        *(uint16_t*)&g_FatCache[fatOffset] = val;
    }
    else
    {
        uint32_t fatOffset = cluster * 2;
        if (fatOffset + 2 > sizeof(g_FatCache)) return;
        *(uint16_t*)&g_FatCache[fatOffset] = (uint16_t)(value & 0xFFFF);
    }
    g_FatCacheDirty = true;
}

static void FAT_FlushFat(void)
{
    if (!g_FatCacheDirty) return;

    // Write Primary FAT
    ATA_WriteSectors(g_Bpb.reserved_sectors, g_Bpb.sectors_per_fat, g_FatCache);

    // Write Secondary FAT copy if present
    if (g_Bpb.fat_count > 1)
    {
        ATA_WriteSectors(g_Bpb.reserved_sectors + g_Bpb.sectors_per_fat, g_Bpb.sectors_per_fat, g_FatCache);
    }

    g_FatCacheDirty = false;
}

static uint32_t FAT_AllocateCluster(void)
{
    for (uint32_t cl = 2; cl < g_TotalClusters + 2; cl++)
    {
        uint32_t val = 0;
        if (g_FatType == 12)
        {
            uint32_t fatOffset = cl + (cl / 2);
            uint16_t raw = *(uint16_t*)&g_FatCache[fatOffset];
            val = (cl & 1) ? (raw >> 4) : (raw & 0x0FFF);
        }
        else
        {
            val = *(uint16_t*)&g_FatCache[cl * 2];
        }

        if (val == 0)
        {
            FAT_SetCluster(cl, (g_FatType == 12) ? 0x0FFF : 0xFFFF);
            FAT_FlushFat();

            uint8_t zeroBuf[512];
            memset(zeroBuf, 0, 512);
            uint32_t lba = FAT_ClusterToLba(cl);
            for (uint8_t s = 0; s < g_Bpb.sectors_per_cluster; s++)
            {
                ATA_WriteSectors(lba + s, 1, zeroBuf);
            }
            return cl;
        }
    }
    return 0;
}

bool FAT_Initialize(void)
{
    g_Mounted = false;
    memset(g_Handles, 0, sizeof(g_Handles));

    if (!ATA_IsAvailable())
    {
        log_warn("FAT", "ATA controller not available, mounting aborted.");
        return false;
    }

    // Read BPB from Sector 0
    uint8_t bootSector[512];
    if (!ATA_ReadSectors(0, 1, bootSector))
    {
        log_err("FAT", "Failed to read Sector 0 (BPB) from ATA device.");
        return false;
    }

    memcpy(&g_Bpb, bootSector, sizeof(FAT_BPB));

    if (g_Bpb.bytes_per_sector != 512 || g_Bpb.sectors_per_cluster == 0)
    {
        log_err("FAT", "Invalid BPB parameters: bytes_per_sector=%u, sectors_per_cluster=%u",
                g_Bpb.bytes_per_sector, g_Bpb.sectors_per_cluster);
        return false;
    }

    uint32_t totalSectors = g_Bpb.total_sectors_short;
    if (totalSectors == 0)
    {
        totalSectors = g_Bpb.total_sectors_large;
    }

    g_RootDirLba = g_Bpb.reserved_sectors + (g_Bpb.fat_count * g_Bpb.sectors_per_fat);
    g_RootDirSectors = (g_Bpb.root_entry_count * sizeof(FAT_DirEntry) + g_Bpb.bytes_per_sector - 1) / g_Bpb.bytes_per_sector;
    g_DataSectionLba = g_RootDirLba + g_RootDirSectors;

    uint32_t dataSectors = totalSectors - g_DataSectionLba;
    g_TotalClusters = dataSectors / g_Bpb.sectors_per_cluster;

    if (g_TotalClusters < 4085)
    {
        g_FatType = 12;
    }
    else
    {
        g_FatType = 16;
    }

    // Read FAT table into memory cache
    uint32_t fatSectorsToRead = g_Bpb.sectors_per_fat;
    if (fatSectorsToRead > FAT_CACHE_SECTORS)
    {
        fatSectorsToRead = FAT_CACHE_SECTORS;
    }

    if (!ATA_ReadSectors(g_Bpb.reserved_sectors, fatSectorsToRead, g_FatCache))
    {
        log_err("FAT", "Failed to load FAT table into cache.");
        return false;
    }

    g_Mounted = true;
    log_info("FAT", "Filesystem mounted: FAT%u, SectorsPerCluster=%u, RootLBA=%u, DataLBA=%u, TotalClusters=%u",
             g_FatType, g_Bpb.sectors_per_cluster, g_RootDirLba, g_DataSectionLba, g_TotalClusters);

    return true;
}

bool FAT_IsMounted(void)
{
    return g_Mounted;
}

static bool FAT_FindFileInDir(uint32_t dirStartLba, uint32_t dirSectorCount, uint32_t dirCluster,
                              const uint8_t dosName[11], FAT_DirEntry* outEntry, uint32_t* outLba, uint32_t* outIndex)
{
    uint8_t buffer[512];
    FAT_DirEntry* entries = (FAT_DirEntry*)buffer;

    if (dirCluster == 0) // Root Directory on FAT12/FAT16
    {
        for (uint32_t s = 0; s < dirSectorCount; s++)
        {
            uint32_t curLba = dirStartLba + s;
            if (!ATA_ReadSectors(curLba, 1, buffer))
                return false;

            for (uint32_t e = 0; e < 16; e++)
            {
                if (entries[e].name[0] == 0x00)
                    return false; // No more entries
                if (entries[e].name[0] == 0xE5)
                    continue;     // Deleted entry
                if (entries[e].attributes & FAT_ATTR_VOLUME_ID)
                    continue;

                if (memcmp(entries[e].name, dosName, 11) == 0)
                {
                    if (outEntry) *outEntry = entries[e];
                    if (outLba)   *outLba = curLba;
                    if (outIndex) *outIndex = e;
                    return true;
                }
            }
        }
        return false;
    }

    // Subdirectory: follow cluster chain
    uint32_t cluster = dirCluster;
    while (cluster >= 2 && cluster < 0x0FFFFFFF)
    {
        uint32_t clusterLba = FAT_ClusterToLba(cluster);
        for (uint32_t s = 0; s < g_Bpb.sectors_per_cluster; s++)
        {
            uint32_t curLba = clusterLba + s;
            if (!ATA_ReadSectors(curLba, 1, buffer))
                return false;

            for (uint32_t e = 0; e < 16; e++)
            {
                if (entries[e].name[0] == 0x00)
                    return false; // No more entries
                if (entries[e].name[0] == 0xE5)
                    continue;     // Deleted entry
                if (entries[e].attributes & FAT_ATTR_VOLUME_ID)
                    continue;

                if (memcmp(entries[e].name, dosName, 11) == 0)
                {
                    if (outEntry) *outEntry = entries[e];
                    if (outLba)   *outLba = curLba;
                    if (outIndex) *outIndex = e;
                    return true;
                }
            }
        }
        cluster = FAT_GetNextCluster(cluster);
    }
    return false;
}

static bool FAT_ResolvePath(const char* path, FAT_DirEntry* outEntry, uint32_t* outLba, uint32_t* outIndex)
{
    if (!g_Mounted || !path) return false;

    // Skip leading slash
    while (*path == '/' || *path == '\\')
        path++;

    if (*path == '\0')
    {
        // Root directory itself
        if (outEntry)
        {
            memset(outEntry, 0, sizeof(FAT_DirEntry));
            outEntry->attributes = FAT_ATTR_DIRECTORY;
        }
        if (outLba)   *outLba = g_RootDirLba;
        if (outIndex) *outIndex = 0;
        return true;
    }

    uint32_t curDirLba = g_RootDirLba;
    uint32_t curDirSectors = g_RootDirSectors;
    uint32_t curDirCluster = 0;

    char part[64];
    while (*path)
    {
        int p = 0;
        while (*path && *path != '/' && *path != '\\' && p < 63)
        {
            part[p++] = *path++;
        }
        part[p] = '\0';

        while (*path == '/' || *path == '\\')
            path++;

        uint8_t dosName[11];
        FAT_ToDosFileName(part, dosName);

        FAT_DirEntry entry;
        uint32_t entryLba = 0;
        uint32_t entryIdx = 0;
        if (!FAT_FindFileInDir(curDirLba, curDirSectors, curDirCluster, dosName, &entry, &entryLba, &entryIdx))
        {
            return false;
        }

        if (*path == '\0')
        {
            // Found target
            if (outEntry) *outEntry = entry;
            if (outLba)   *outLba = entryLba;
            if (outIndex) *outIndex = entryIdx;
            return true;
        }

        // Subdirectory check
        if (!(entry.attributes & FAT_ATTR_DIRECTORY))
        {
            return false;
        }

        curDirCluster = entry.first_cluster_low | ((uint32_t)entry.first_cluster_high << 16);
        curDirLba = FAT_ClusterToLba(curDirCluster);
        curDirSectors = g_Bpb.sectors_per_cluster;
    }

    return false;
}

bool FAT_CreateFile(const char* path)
{
    if (!g_Mounted || !path) return false;

    // Check if already exists
    FAT_DirEntry existing;
    if (FAT_ResolvePath(path, &existing, NULL, NULL))
    {
        return true;
    }

    uint8_t dosName[11];
    FAT_ToDosFileName(path, dosName);

    uint8_t buffer[512];
    FAT_DirEntry* entries = (FAT_DirEntry*)buffer;

    // Find free entry in Root Directory
    for (uint32_t s = 0; s < g_RootDirSectors; s++)
    {
        uint32_t curLba = g_RootDirLba + s;
        if (!ATA_ReadSectors(curLba, 1, buffer))
            return false;

        for (uint32_t e = 0; e < 16; e++)
        {
            if (entries[e].name[0] == 0x00 || entries[e].name[0] == 0xE5)
            {
                memset(&entries[e], 0, sizeof(FAT_DirEntry));
                memcpy(entries[e].name, dosName, 11);
                entries[e].attributes = FAT_ATTR_ARCHIVE;
                entries[e].file_size = 0;
                entries[e].first_cluster_low = 0;
                entries[e].first_cluster_high = 0;

                ATA_WriteSectors(curLba, 1, buffer);
                ATA_FlushCache();
                return true;
            }
        }
    }

    return false;
}

FAT_Handle* FAT_Open(const char* path, const char* mode)
{
    if (!g_Mounted || !path || !mode) return NULL;

    bool writeMode = (mode[0] == 'w' || mode[0] == 'a');
    bool appendMode = (mode[0] == 'a');

    FAT_DirEntry entry;
    uint32_t dirLba = 0;
    uint32_t dirIdx = 0;
    bool found = FAT_ResolvePath(path, &entry, &dirLba, &dirIdx);

    if (!found)
    {
        if (writeMode)
        {
            if (!FAT_CreateFile(path))
                return NULL;
            found = FAT_ResolvePath(path, &entry, &dirLba, &dirIdx);
            if (!found) return NULL;
        }
        else
        {
            return NULL;
        }
    }

    // Allocate handle
    FAT_Handle* h = NULL;
    for (int i = 0; i < MAX_OPEN_FILES; i++)
    {
        if (!g_Handles[i].used)
        {
            h = &g_Handles[i];
            break;
        }
    }

    if (!h) return NULL;

    memset(h, 0, sizeof(FAT_Handle));
    h->used = true;
    h->is_dir = (entry.attributes & FAT_ATTR_DIRECTORY) != 0;
    h->write_mode = writeMode;
    strncpy(h->path, path, sizeof(h->path) - 1);
    h->first_cluster = entry.first_cluster_low | ((uint32_t)entry.first_cluster_high << 16);
    h->current_cluster = h->first_cluster;
    if (mode[0] == 'w')
    {
        h->file_size = 0;
        h->position = 0;
        uint8_t sectorBuf[512];
        if (ATA_ReadSectors(dirLba, 1, sectorBuf))
        {
            FAT_DirEntry* entries = (FAT_DirEntry*)sectorBuf;
            entries[dirIdx].file_size = 0;
            ATA_WriteSectors(dirLba, 1, sectorBuf);
        }
    }
    else
    {
        h->file_size = entry.file_size;
        h->position = appendMode ? entry.file_size : 0;
    }
    h->dir_sector_lba = dirLba;
    h->dir_entry_index = dirIdx;
    h->buffer_lba = 0xFFFFFFFF;

    return h;
}

int FAT_Read(FAT_Handle* handle, void* buffer, size_t count)
{
    if (!g_Mounted || !handle || !handle->used || !buffer || count == 0)
        return 0;

    if (handle->position >= handle->file_size && !handle->is_dir)
        return 0;

    if (!handle->is_dir && (handle->position + count > handle->file_size))
    {
        count = handle->file_size - handle->position;
    }

    uint8_t* out = (uint8_t*)buffer;
    size_t bytesRead = 0;

    while (bytesRead < count)
    {
        if (handle->current_cluster < 2 || handle->current_cluster >= 0x0FF8)
            break;

        uint32_t sectorInCluster = (handle->position / 512) % g_Bpb.sectors_per_cluster;
        uint32_t offsetInSector = handle->position % 512;
        uint32_t targetLba = FAT_ClusterToLba(handle->current_cluster) + sectorInCluster;

        if (handle->buffer_lba != targetLba)
        {
            if (!ATA_ReadSectors(targetLba, 1, handle->sector_buffer))
                break;
            handle->buffer_lba = targetLba;
        }

        uint32_t chunk = 512 - offsetInSector;
        if (chunk > (count - bytesRead))
            chunk = (count - bytesRead);

        memcpy(out + bytesRead, handle->sector_buffer + offsetInSector, chunk);

        bytesRead += chunk;
        handle->position += chunk;

        if ((handle->position % 512) == 0)
        {
            uint32_t nextSec = (handle->position / 512) % g_Bpb.sectors_per_cluster;
            if (nextSec == 0)
            {
                handle->current_cluster = FAT_GetNextCluster(handle->current_cluster);
            }
        }
    }

    return (int)bytesRead;
}

int FAT_Write(FAT_Handle* handle, const void* buffer, size_t count)
{
    if (!g_Mounted || !handle || !handle->used || !handle->write_mode || !buffer || count == 0)
        return 0;

    const uint8_t* in = (const uint8_t*)buffer;
    size_t bytesWritten = 0;

    // Allocate first cluster if needed
    if (handle->first_cluster == 0)
    {
        handle->first_cluster = FAT_AllocateCluster();
        if (handle->first_cluster == 0)
            return 0; // Disk full
        handle->current_cluster = handle->first_cluster;
    }

    while (bytesWritten < count)
    {
        uint32_t sectorInCluster = (handle->position / 512) % g_Bpb.sectors_per_cluster;
        uint32_t offsetInSector = handle->position % 512;
        uint32_t targetLba = FAT_ClusterToLba(handle->current_cluster) + sectorInCluster;

        if (handle->buffer_lba != targetLba)
        {
            if (offsetInSector != 0)
            {
                ATA_ReadSectors(targetLba, 1, handle->sector_buffer);
            }
            else
            {
                memset(handle->sector_buffer, 0, 512);
            }
            handle->buffer_lba = targetLba;
        }

        uint32_t chunk = 512 - offsetInSector;
        if (chunk > (count - bytesWritten))
            chunk = (count - bytesWritten);

        memcpy(handle->sector_buffer + offsetInSector, in + bytesWritten, chunk);
        ATA_WriteSectors(targetLba, 1, handle->sector_buffer);

        bytesWritten += chunk;
        handle->position += chunk;
        if (handle->position > handle->file_size)
        {
            handle->file_size = handle->position;
        }

        if ((handle->position % 512) == 0)
        {
            uint32_t nextSec = (handle->position / 512) % g_Bpb.sectors_per_cluster;
            if (nextSec == 0)
            {
                uint32_t next = FAT_GetNextCluster(handle->current_cluster);
                if (next >= 0x0FF8 || next == 0)
                {
                    uint32_t newCl = FAT_AllocateCluster();
                    if (newCl == 0) break;
                    FAT_SetCluster(handle->current_cluster, newCl);
                    FAT_FlushFat();
                    handle->current_cluster = newCl;
                }
                else
                {
                    handle->current_cluster = next;
                }
            }
        }
    }

    return (int)bytesWritten;
}

bool FAT_Seek(FAT_Handle* handle, uint32_t offset)
{
    if (!g_Mounted || !handle || !handle->used)
        return false;

    if (!handle->is_dir && offset > handle->file_size)
        offset = handle->file_size;

    // Reset to beginning of file
    handle->current_cluster = handle->first_cluster;
    handle->position = 0;
    handle->buffer_lba = 0xFFFFFFFF;

    uint32_t clusterBytes = (uint32_t)g_Bpb.sectors_per_cluster * 512;
    if (clusterBytes == 0)
        clusterBytes = 512;

    uint32_t clustersToSkip = offset / clusterBytes;

    for (uint32_t i = 0; i < clustersToSkip; i++)
    {
        if (handle->current_cluster < 2 || handle->current_cluster >= 0x0FF8)
            return false;
        handle->current_cluster = FAT_GetNextCluster(handle->current_cluster);
    }

    handle->position = offset;
    return true;
}

int FAT_SeekEx(FAT_Handle* handle, int32_t offset, int whence)
{
    if (!g_Mounted || !handle || !handle->used)
        return -1;

    int32_t target = 0;
    if (whence == 0) // SEEK_SET
    {
        target = offset;
    }
    else if (whence == 1) // SEEK_CUR
    {
        target = (int32_t)handle->position + offset;
    }
    else if (whence == 2) // SEEK_END
    {
        target = (int32_t)handle->file_size + offset;
    }
    else
    {
        return -1;
    }

    if (target < 0)
        return -1;

    if (!FAT_Seek(handle, (uint32_t)target))
        return -1;

    return (int)handle->position;
}

uint32_t FAT_Tell(FAT_Handle* handle)
{
    return handle ? handle->position : 0;
}

uint32_t FAT_GetSize(FAT_Handle* handle)
{
    return handle ? handle->file_size : 0;
}

bool FAT_Close(FAT_Handle* handle)
{
    if (!g_Mounted || !handle || !handle->used)
        return false;

    if (handle->write_mode)
    {
        // Update directory entry with final size and first cluster
        if (handle->dir_sector_lba != 0)
        {
            uint8_t dirBuf[512];
            if (ATA_ReadSectors(handle->dir_sector_lba, 1, dirBuf))
            {
                FAT_DirEntry* entries = (FAT_DirEntry*)dirBuf;
                entries[handle->dir_entry_index].file_size = handle->file_size;
                entries[handle->dir_entry_index].first_cluster_low = (uint16_t)(handle->first_cluster & 0xFFFF);
                entries[handle->dir_entry_index].first_cluster_high = (uint16_t)((handle->first_cluster >> 16) & 0xFFFF);
                ATA_WriteSectors(handle->dir_sector_lba, 1, dirBuf);
            }
        }
        FAT_FlushFat();
        ATA_FlushCache();
    }

    handle->used = false;
    return true;
}

bool FAT_ReadDir(const char* path, size_t index, FAT_DirEntryInfo* out)
{
    if (!g_Mounted || !out) return false;

    uint32_t dirStartLba = g_RootDirLba;
    uint32_t dirSectors = g_RootDirSectors;
    uint32_t dirCluster = 0;

    if (path && path[0] && strcmp(path, "/") != 0)
    {
        FAT_DirEntry dirEntry;
        if (!FAT_ResolvePath(path, &dirEntry, NULL, NULL) || !(dirEntry.attributes & FAT_ATTR_DIRECTORY))
        {
            return false;
        }
        dirCluster = dirEntry.first_cluster_low | ((uint32_t)dirEntry.first_cluster_high << 16);
        dirStartLba = FAT_ClusterToLba(dirCluster);
        dirSectors = g_Bpb.sectors_per_cluster;
    }

    uint8_t buffer[512];
    FAT_DirEntry* entries = (FAT_DirEntry*)buffer;
    size_t validCount = 0;

    if (dirCluster == 0) // Root Directory on FAT12/FAT16
    {
        for (uint32_t s = 0; s < dirSectors; s++)
        {
            if (!ATA_ReadSectors(dirStartLba + s, 1, buffer))
                return false;

            for (uint32_t e = 0; e < 16; e++)
            {
                if (entries[e].name[0] == 0x00)
                    return false;
                if (entries[e].name[0] == 0xE5)
                    continue;
                if (entries[e].attributes & FAT_ATTR_VOLUME_ID)
                    continue;

                if (validCount == index)
                {
                    FAT_FormatFileName(entries[e].name, out->name);
                    out->size = entries[e].file_size;
                    out->attributes = entries[e].attributes;
                    out->is_directory = (entries[e].attributes & FAT_ATTR_DIRECTORY) != 0;
                    return true;
                }
                validCount++;
            }
        }
        return false;
    }

    // Subdirectory: follow cluster chain
    uint32_t cluster = dirCluster;
    while (cluster >= 2 && cluster < 0x0FFFFFFF)
    {
        uint32_t clusterLba = FAT_ClusterToLba(cluster);
        for (uint32_t s = 0; s < g_Bpb.sectors_per_cluster; s++)
        {
            if (!ATA_ReadSectors(clusterLba + s, 1, buffer))
                return false;

            for (uint32_t e = 0; e < 16; e++)
            {
                if (entries[e].name[0] == 0x00)
                    return false;
                if (entries[e].name[0] == 0xE5)
                    continue;
                if (entries[e].attributes & FAT_ATTR_VOLUME_ID)
                    continue;

                if (validCount == index)
                {
                    FAT_FormatFileName(entries[e].name, out->name);
                    out->size = entries[e].file_size;
                    out->attributes = entries[e].attributes;
                    out->is_directory = (entries[e].attributes & FAT_ATTR_DIRECTORY) != 0;
                    return true;
                }
                validCount++;
            }
        }
        cluster = FAT_GetNextCluster(cluster);
    }

    return false;
}

bool FAT_Stat(const char* path, FAT_DirEntryInfo* out)
{
    if (!g_Mounted || !path || !out) return false;

    FAT_DirEntry entry;
    if (!FAT_ResolvePath(path, &entry, NULL, NULL))
        return false;

    FAT_FormatFileName(entry.name, out->name);
    out->size = entry.file_size;
    out->attributes = entry.attributes;
    out->is_directory = (entry.attributes & FAT_ATTR_DIRECTORY) != 0;
    return true;
}
