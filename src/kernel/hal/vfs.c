#include "vfs.h"
#include "fat.h"
#include <arch/i686/vga_text.h>
#include <arch/i686/e9.h>
#include <arch/i686/keyboard.h>
#include <memory.h>
#include <debug.h>

#define MAX_VFS_DESCRIPTORS 16

static FAT_Handle* g_VfsDescriptors[MAX_VFS_DESCRIPTORS];

void VFS_Initialize(void)
{
    memset(g_VfsDescriptors, 0, sizeof(g_VfsDescriptors));
    log_info("VFS", "Virtual File System initialized (stdin=0, stdout=1, stderr=2, debug=3, files=4..19).");
}

fd_t vfs_open(const char* path, int flags)
{
    if (!path) return -1;

    // Find free VFS descriptor
    int freeSlot = -1;
    for (int i = 0; i < MAX_VFS_DESCRIPTORS; i++)
    {
        if (g_VfsDescriptors[i] == NULL)
        {
            freeSlot = i;
            break;
        }
    }

    if (freeSlot < 0)
    {
        log_warn("VFS", "Out of file descriptors for path '%s'", path);
        return -1;
    }

    const char* mode = "r";
    if (flags & (VFS_O_WRONLY | VFS_O_RDWR))
    {
        if (flags & VFS_O_APPEND)
            mode = "a";
        else
            mode = "w";
    }

    if (flags & VFS_O_CREAT)
    {
        FAT_CreateFile(path);
    }

    FAT_Handle* handle = FAT_Open(path, mode);
    if (!handle)
    {
        return -1;
    }

    g_VfsDescriptors[freeSlot] = handle;
    return freeSlot + 4;
}

int vfs_read(fd_t fd, void* buf, size_t count)
{
    if (!buf || count == 0) return 0;

    if (fd == VFS_FD_STDIN)
    {
        char* cbuf = (char*)buf;
        size_t n = 0;
        while (n < count)
        {
            char c = i686_Keyboard_GetChar();
            if (c == 0) break;
            cbuf[n++] = c;
            if (c == '\n') break;
        }
        return (int)n;
    }

    if (fd >= 4 && fd < (4 + MAX_VFS_DESCRIPTORS))
    {
        int slot = fd - 4;
        if (g_VfsDescriptors[slot] == NULL)
            return -1;

        return FAT_Read(g_VfsDescriptors[slot], buf, count);
    }

    return -1;
}

int vfs_write(fd_t fd, const void* buf, size_t count)
{
    if (!buf || count == 0) return 0;

    switch (fd)
    {
    case VFS_FD_STDIN:
        return 0;

    case VFS_FD_STDOUT:
    case VFS_FD_STDERR:
        for (size_t i = 0; i < count; i++)
        {
            char c = ((const char*)buf)[i];
            VGA_putc(c);
            e9_putc(c);
        }
        return count;

    case VFS_FD_DEBUG:
        for (size_t i = 0; i < count; i++)
            e9_putc(((const char*)buf)[i]);
        return count;

    default:
        if (fd >= 4 && fd < (4 + MAX_VFS_DESCRIPTORS))
        {
            int slot = fd - 4;
            if (g_VfsDescriptors[slot] == NULL)
                return -1;

            return FAT_Write(g_VfsDescriptors[slot], buf, count);
        }
        return -1;
    }
}

int vfs_lseek(fd_t fd, int32_t offset, int whence)
{
    if (fd < 4 || fd >= (4 + MAX_VFS_DESCRIPTORS))
        return -1;

    int slot = fd - 4;
    FAT_Handle* handle = g_VfsDescriptors[slot];
    if (!handle)
        return -1;

    return FAT_SeekEx(handle, offset, whence);
}

int vfs_close(fd_t fd)
{
    if (fd < 4) return 0; // Standard streams remain open

    if (fd >= 4 && fd < (4 + MAX_VFS_DESCRIPTORS))
    {
        int slot = fd - 4;
        if (g_VfsDescriptors[slot] == NULL)
            return -1;

        FAT_Close(g_VfsDescriptors[slot]);
        g_VfsDescriptors[slot] = NULL;
        return 0;
    }

    return -1;
}

int vfs_readdir(const char* path, size_t index, VFS_DirEntry* entry)
{
    if (!entry) return -1;
    return FAT_ReadDir(path, index, (FAT_DirEntryInfo*)entry) ? 0 : -1;
}

int vfs_stat(const char* path, VFS_Stat* st)
{
    if (!path || !st) return -1;

    FAT_DirEntryInfo info;
    if (!FAT_Stat(path, &info))
        return -1;

    st->size = info.size;
    st->is_directory = info.is_directory;
    st->attributes = info.attributes;
    return 0;
}

int vfs_touch(const char* path)
{
    if (!path) return -1;
    return FAT_CreateFile(path) ? 0 : -1;
}

int VFS_Write(fd_t file, uint8_t* data, size_t size)
{
    return vfs_write(file, data, size);
}
