#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef int fd_t;

#define VFS_FD_STDIN    0
#define VFS_FD_STDOUT   1
#define VFS_FD_STDERR   2
#define VFS_FD_DEBUG    3

#define VFS_O_RDONLY    0x0000
#define VFS_O_WRONLY    0x0001
#define VFS_O_RDWR      0x0002
#define VFS_O_CREAT     0x0040
#define VFS_O_TRUNC     0x0200
#define VFS_O_APPEND    0x0400

#define VFS_SEEK_SET    0
#define VFS_SEEK_CUR    1
#define VFS_SEEK_END    2

typedef struct {
    char        name[64];
    uint32_t    size;
    uint8_t     attributes;
    bool        is_directory;
} VFS_DirEntry;

typedef struct {
    uint32_t    size;
    bool        is_directory;
    uint8_t     attributes;
} VFS_Stat;

void VFS_Initialize(void);

// POSIX-style VFS API
fd_t vfs_open(const char* path, int flags);
int  vfs_read(fd_t fd, void* buf, size_t count);
int  vfs_write(fd_t fd, const void* buf, size_t count);
int  vfs_lseek(fd_t fd, int32_t offset, int whence);
int  vfs_close(fd_t fd);
int  vfs_readdir(const char* path, size_t index, VFS_DirEntry* entry);
int  vfs_stat(const char* path, VFS_Stat* st);
int  vfs_touch(const char* path);

// Legacy backward-compatibility wrapper
int VFS_Write(fd_t file, uint8_t* data, size_t size);
