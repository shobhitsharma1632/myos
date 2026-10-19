#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define FAT_ATTR_READ_ONLY      0x01
#define FAT_ATTR_HIDDEN         0x02
#define FAT_ATTR_SYSTEM         0x04
#define FAT_ATTR_VOLUME_ID      0x08
#define FAT_ATTR_DIRECTORY      0x10
#define FAT_ATTR_ARCHIVE        0x20
#define FAT_ATTR_LFN            0x0F

typedef struct {
    char        name[64];
    uint32_t    size;
    uint8_t     attributes;
    bool        is_directory;
} FAT_DirEntryInfo;

typedef struct FAT_Handle FAT_Handle;

bool FAT_Initialize(void);
bool FAT_IsMounted(void);

FAT_Handle* FAT_Open(const char* path, const char* mode);
int FAT_Read(FAT_Handle* handle, void* buffer, size_t count);
int FAT_Write(FAT_Handle* handle, const void* buffer, size_t count);
bool FAT_Close(FAT_Handle* handle);
bool FAT_CreateFile(const char* path);
bool FAT_ReadDir(const char* path, size_t index, FAT_DirEntryInfo* out);
bool FAT_Stat(const char* path, FAT_DirEntryInfo* out);

void FAT_FormatFileName(const uint8_t fatName[11], char* outName);
void FAT_ToDosFileName(const char* inName, uint8_t dosName[11]);
