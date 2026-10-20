#include "vfs.h"
#include "fat.h"
#include "pipe.h"
#include "task.h"
#include <arch/i686/vga_text.h>
#include <arch/i686/e9.h>
#include <arch/i686/keyboard.h>
#include <memory.h>
#include <debug.h>

#define MODULE "VFS"
#define MAX_GLOBAL_OPEN_FILES 64

typedef enum {
    VFS_TYPE_FREE = 0,
    VFS_TYPE_CONSOLE_STDIN,
    VFS_TYPE_CONSOLE_STDOUT,
    VFS_TYPE_CONSOLE_STDERR,
    VFS_TYPE_CONSOLE_DEBUG,
    VFS_TYPE_FILE,
    VFS_TYPE_PIPE_READ,
    VFS_TYPE_PIPE_WRITE
} VFS_Type;

typedef struct {
    VFS_Type type;
    int      ref_count;
    int      flags;
    void*    handle; // FAT_Handle* or Pipe*
} VFS_OpenFile;

static VFS_OpenFile g_OpenFiles[MAX_GLOBAL_OPEN_FILES];

void VFS_Initialize(void)
{
    memset(g_OpenFiles, 0, sizeof(g_OpenFiles));

    g_OpenFiles[0].type = VFS_TYPE_CONSOLE_STDIN;
    g_OpenFiles[0].ref_count = 10000;

    g_OpenFiles[1].type = VFS_TYPE_CONSOLE_STDOUT;
    g_OpenFiles[1].ref_count = 10000;

    g_OpenFiles[2].type = VFS_TYPE_CONSOLE_STDERR;
    g_OpenFiles[2].ref_count = 10000;

    g_OpenFiles[3].type = VFS_TYPE_CONSOLE_DEBUG;
    g_OpenFiles[3].ref_count = 10000;

    log_info(MODULE, "Virtual File System initialized with POSIX per-process descriptors and Pipes.");
}

static int vfs_alloc_open_file(void)
{
    for (int i = 4; i < MAX_GLOBAL_OPEN_FILES; i++)
    {
        if (g_OpenFiles[i].type == VFS_TYPE_FREE)
            return i;
    }
    return -1;
}

static int vfs_alloc_task_fd(Task* task)
{
    if (!task) return -1;
    for (int fd = 0; fd < TASK_MAX_FDS; fd++)
    {
        if (task->fd_table[fd] < 0)
            return fd;
    }
    return -1;
}

static VFS_OpenFile* vfs_get_open_file(fd_t fd)
{
    Task* current = Task_GetCurrent();
    if (!current)
    {
        // Early boot fallback (before multitasking)
        if (fd >= 0 && fd < MAX_GLOBAL_OPEN_FILES && g_OpenFiles[fd].type != VFS_TYPE_FREE)
            return &g_OpenFiles[fd];
        return NULL;
    }

    if (fd < 0 || fd >= TASK_MAX_FDS)
        return NULL;

    int slot = current->fd_table[fd];
    if (slot < 0 || slot >= MAX_GLOBAL_OPEN_FILES)
        return NULL;

    if (g_OpenFiles[slot].type == VFS_TYPE_FREE)
        return NULL;

    return &g_OpenFiles[slot];
}

void vfs_init_task_fds(Task* task)
{
    if (!task) return;
    task->fd_table[0] = 0; // stdin
    task->fd_table[1] = 1; // stdout
    task->fd_table[2] = 2; // stderr
    task->fd_table[3] = 3; // debug
    for (int i = 4; i < TASK_MAX_FDS; i++)
    {
        task->fd_table[i] = -1;
    }
}

void vfs_clone_task_fds(Task* parent, Task* child)
{
    if (!child) return;
    if (!parent)
    {
        vfs_init_task_fds(child);
        return;
    }

    for (int i = 0; i < TASK_MAX_FDS; i++)
    {
        child->fd_table[i] = parent->fd_table[i];
        int slot = child->fd_table[i];
        if (slot >= 0 && slot < MAX_GLOBAL_OPEN_FILES)
        {
            g_OpenFiles[slot].ref_count++;
            if (g_OpenFiles[slot].type == VFS_TYPE_PIPE_READ)
            {
                Pipe_RetainReader((Pipe*)g_OpenFiles[slot].handle);
            }
            else if (g_OpenFiles[slot].type == VFS_TYPE_PIPE_WRITE)
            {
                Pipe_RetainWriter((Pipe*)g_OpenFiles[slot].handle);
            }
        }
    }
}

void vfs_close_task_fds(Task* task)
{
    if (!task) return;
    for (int i = 0; i < TASK_MAX_FDS; i++)
    {
        int slot = task->fd_table[i];
        if (slot >= 0 && slot < MAX_GLOBAL_OPEN_FILES)
        {
            task->fd_table[i] = -1;
            g_OpenFiles[slot].ref_count--;
            if (g_OpenFiles[slot].type == VFS_TYPE_PIPE_READ)
            {
                Pipe_CloseReader((Pipe*)g_OpenFiles[slot].handle);
            }
            else if (g_OpenFiles[slot].type == VFS_TYPE_PIPE_WRITE)
            {
                Pipe_CloseWriter((Pipe*)g_OpenFiles[slot].handle);
            }
            else if (g_OpenFiles[slot].type == VFS_TYPE_FILE)
            {
                if (g_OpenFiles[slot].ref_count <= 0)
                {
                    FAT_Close((FAT_Handle*)g_OpenFiles[slot].handle);
                }
            }

            if (g_OpenFiles[slot].ref_count <= 0 && slot >= 4)
            {
                g_OpenFiles[slot].type = VFS_TYPE_FREE;
                g_OpenFiles[slot].handle = NULL;
                g_OpenFiles[slot].flags = 0;
            }
        }
    }
}

fd_t vfs_open(const char* path, int flags)
{
    if (!path) return -1;
    Task* current = Task_GetCurrent();

    int fd = -1;
    if (current)
    {
        fd = vfs_alloc_task_fd(current);
        if (fd < 0) return -1;
    }

    int slot = vfs_alloc_open_file();
    if (slot < 0) return -1;

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

    g_OpenFiles[slot].type = VFS_TYPE_FILE;
    g_OpenFiles[slot].ref_count = 1;
    g_OpenFiles[slot].flags = flags;
    g_OpenFiles[slot].handle = handle;

    if (current)
    {
        current->fd_table[fd] = slot;
        return fd;
    }

    return slot;
}

int vfs_read(fd_t fd, void* buf, size_t count)
{
    if (!buf || count == 0) return 0;
    VFS_OpenFile* file = vfs_get_open_file(fd);
    if (!file) return -1;

    switch (file->type)
    {
        case VFS_TYPE_CONSOLE_STDIN:
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

        case VFS_TYPE_FILE:
            return FAT_Read((FAT_Handle*)file->handle, buf, count);

        case VFS_TYPE_PIPE_READ:
            return Pipe_Read((Pipe*)file->handle, buf, count);

        default:
            return -1;
    }
}

int vfs_write(fd_t fd, const void* buf, size_t count)
{
    if (!buf || count == 0) return 0;
    VFS_OpenFile* file = vfs_get_open_file(fd);
    if (!file) return -1;

    switch (file->type)
    {
        case VFS_TYPE_CONSOLE_STDIN:
            return 0;

        case VFS_TYPE_CONSOLE_STDOUT:
        case VFS_TYPE_CONSOLE_STDERR:
            for (size_t i = 0; i < count; i++)
            {
                char c = ((const char*)buf)[i];
                VGA_putc(c);
                e9_putc(c);
            }
            return (int)count;

        case VFS_TYPE_CONSOLE_DEBUG:
            for (size_t i = 0; i < count; i++)
                e9_putc(((const char*)buf)[i]);
            return (int)count;

        case VFS_TYPE_FILE:
            return FAT_Write((FAT_Handle*)file->handle, buf, count);

        case VFS_TYPE_PIPE_WRITE:
            return Pipe_Write((Pipe*)file->handle, buf, count);

        default:
            return -1;
    }
}

int vfs_lseek(fd_t fd, int32_t offset, int whence)
{
    VFS_OpenFile* file = vfs_get_open_file(fd);
    if (!file || file->type != VFS_TYPE_FILE)
        return -1;

    return FAT_SeekEx((FAT_Handle*)file->handle, offset, whence);
}

int vfs_close(fd_t fd)
{
    Task* current = Task_GetCurrent();
    if (!current)
    {
        if (fd < 4) return 0;
        if (fd >= 4 && fd < MAX_GLOBAL_OPEN_FILES && g_OpenFiles[fd].type != VFS_TYPE_FREE)
        {
            if (g_OpenFiles[fd].type == VFS_TYPE_FILE)
                FAT_Close((FAT_Handle*)g_OpenFiles[fd].handle);
            g_OpenFiles[fd].type = VFS_TYPE_FREE;
            g_OpenFiles[fd].handle = NULL;
            return 0;
        }
        return -1;
    }

    if (fd < 0 || fd >= TASK_MAX_FDS) return -1;
    int slot = current->fd_table[fd];
    if (slot < 0 || slot >= MAX_GLOBAL_OPEN_FILES) return -1;

    current->fd_table[fd] = -1;
    g_OpenFiles[slot].ref_count--;

    if (g_OpenFiles[slot].type == VFS_TYPE_PIPE_READ)
    {
        Pipe_CloseReader((Pipe*)g_OpenFiles[slot].handle);
    }
    else if (g_OpenFiles[slot].type == VFS_TYPE_PIPE_WRITE)
    {
        Pipe_CloseWriter((Pipe*)g_OpenFiles[slot].handle);
    }
    else if (g_OpenFiles[slot].type == VFS_TYPE_FILE)
    {
        if (g_OpenFiles[slot].ref_count <= 0)
        {
            FAT_Close((FAT_Handle*)g_OpenFiles[slot].handle);
        }
    }

    if (g_OpenFiles[slot].ref_count <= 0 && slot >= 4)
    {
        g_OpenFiles[slot].type = VFS_TYPE_FREE;
        g_OpenFiles[slot].handle = NULL;
        g_OpenFiles[slot].flags = 0;
    }

    return 0;
}

int vfs_pipe(int pipefd[2])
{
    if (!pipefd) return -1;
    Task* current = Task_GetCurrent();
    if (!current) return -1;

    int fd0 = -1, fd1 = -1;
    for (int i = 0; i < TASK_MAX_FDS; i++)
    {
        if (current->fd_table[i] < 0)
        {
            if (fd0 < 0) fd0 = i;
            else if (fd1 < 0) { fd1 = i; break; }
        }
    }
    if (fd0 < 0 || fd1 < 0) return -1;

    int slot0 = -1, slot1 = -1;
    for (int i = 4; i < MAX_GLOBAL_OPEN_FILES; i++)
    {
        if (g_OpenFiles[i].type == VFS_TYPE_FREE)
        {
            if (slot0 < 0) slot0 = i;
            else if (slot1 < 0) { slot1 = i; break; }
        }
    }
    if (slot0 < 0 || slot1 < 0) return -1;

    Pipe* pipe = Pipe_Create();
    if (!pipe) return -1;

    g_OpenFiles[slot0].type = VFS_TYPE_PIPE_READ;
    g_OpenFiles[slot0].ref_count = 1;
    g_OpenFiles[slot0].flags = VFS_O_RDONLY;
    g_OpenFiles[slot0].handle = pipe;

    g_OpenFiles[slot1].type = VFS_TYPE_PIPE_WRITE;
    g_OpenFiles[slot1].ref_count = 1;
    g_OpenFiles[slot1].flags = VFS_O_WRONLY;
    g_OpenFiles[slot1].handle = pipe;

    current->fd_table[fd0] = slot0;
    current->fd_table[fd1] = slot1;

    pipefd[0] = fd0;
    pipefd[1] = fd1;
    return 0;
}

int vfs_dup(int oldfd)
{
    Task* current = Task_GetCurrent();
    if (!current) return -1;
    int newfd = vfs_alloc_task_fd(current);
    if (newfd < 0) return -1;
    return vfs_dup2(oldfd, newfd);
}

int vfs_dup2(int oldfd, int newfd)
{
    Task* current = Task_GetCurrent();
    if (!current) return -1;

    if (oldfd < 0 || oldfd >= TASK_MAX_FDS || newfd < 0 || newfd >= TASK_MAX_FDS)
        return -1;

    int old_slot = current->fd_table[oldfd];
    if (old_slot < 0 || old_slot >= MAX_GLOBAL_OPEN_FILES || g_OpenFiles[old_slot].type == VFS_TYPE_FREE)
        return -1;

    if (oldfd == newfd)
        return newfd;

    // Close newfd if already open
    if (current->fd_table[newfd] >= 0)
    {
        vfs_close(newfd);
    }

    current->fd_table[newfd] = old_slot;
    g_OpenFiles[old_slot].ref_count++;
    if (g_OpenFiles[old_slot].type == VFS_TYPE_PIPE_READ)
    {
        Pipe_RetainReader((Pipe*)g_OpenFiles[old_slot].handle);
    }
    else if (g_OpenFiles[old_slot].type == VFS_TYPE_PIPE_WRITE)
    {
        Pipe_RetainWriter((Pipe*)g_OpenFiles[old_slot].handle);
    }

    return newfd;
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
