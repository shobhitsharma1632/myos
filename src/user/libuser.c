#include "libuser.h"

// ============================================================================
// Low-Level Syscall Dispatchers
// ============================================================================

static inline int32_t syscall0(uint32_t num)
{
    int32_t ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num) : "memory");
    return ret;
}

static inline int32_t syscall1(uint32_t num, uint32_t arg1)
{
    int32_t ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1) : "memory");
    return ret;
}

static inline int32_t syscall2(uint32_t num, uint32_t arg1, uint32_t arg2)
{
    int32_t ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1), "c"(arg2) : "memory");
    return ret;
}

static inline int32_t syscall3(uint32_t num, uint32_t arg1, uint32_t arg2, uint32_t arg3)
{
    int32_t ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3) : "memory");
    return ret;
}

void sys_exit(int code)
{
    syscall1(0, (uint32_t)code);
    while (1) { __asm__ volatile("hlt"); }
}

void sys_print(const char* str)
{
    if (!str) return;
    syscall2(1, (uint32_t)str, strlen(str));
}

void sys_println(const char* str)
{
    sys_print(str);
    sys_print("\n");
}

int sys_read(char* buf, int max_len)
{
    return syscall2(2, (uint32_t)buf, (uint32_t)max_len);
}

void sys_yield(void)
{
    syscall0(3);
}

int sys_getpid(void)
{
    return syscall0(4);
}

void sys_sleep(uint32_t ticks)
{
    syscall1(5, ticks);
}

uint32_t sys_getticks(void)
{
    return (uint32_t)syscall0(6);
}

void* sys_sbrk(int incr)
{
    return (void*)syscall1(7, (uint32_t)incr);
}

int sys_open(const char* path, int flags)
{
    return syscall2(10, (uint32_t)path, (uint32_t)flags);
}

int sys_close(int fd)
{
    return syscall1(11, (uint32_t)fd);
}

int sys_read_file(int fd, void* buf, size_t count)
{
    return syscall3(12, (uint32_t)fd, (uint32_t)buf, (uint32_t)count);
}

int sys_write_file(int fd, const void* buf, size_t count)
{
    return syscall3(13, (uint32_t)fd, (uint32_t)buf, (uint32_t)count);
}

int sys_lseek(int fd, int offset, int whence)
{
    return syscall3(18, (uint32_t)fd, (uint32_t)offset, (uint32_t)whence);
}

int sys_spawn(const char* path, int argc, char* const argv[])
{
    return syscall3(16, (uint32_t)path, (uint32_t)argc, (uint32_t)argv);
}

int sys_waitpid(int pid, int* status)
{
    return syscall2(17, (uint32_t)pid, (uint32_t)status);
}

int sys_pipe(int pipefd[2])
{
    return syscall1(22, (uint32_t)pipefd);
}

int sys_dup(int oldfd)
{
    return syscall1(23, (uint32_t)oldfd);
}

int sys_dup2(int oldfd, int newfd)
{
    return syscall2(24, (uint32_t)oldfd, (uint32_t)newfd);
}

int pipe(int pipefd[2])
{
    return sys_pipe(pipefd);
}

int dup(int oldfd)
{
    return sys_dup(oldfd);
}

int dup2(int oldfd, int newfd)
{
    return sys_dup2(oldfd, newfd);
}

// ============================================================================
// User-Space Dynamic Memory Management (malloc, free, calloc, realloc)
// ============================================================================

typedef struct BlockHeader {
    size_t size;               // Usable payload size in bytes
    bool is_free;              // Free status flag
    struct BlockHeader* next;  // Next block in address order
    struct BlockHeader* prev;  // Previous block in address order
} BlockHeader;

#define BLOCK_HEADER_SIZE sizeof(BlockHeader)
#define ALIGN8(s) (((s) + 7) & ~7)

static BlockHeader* g_heap_head = NULL;

void* malloc(size_t size)
{
    if (size == 0) return NULL;

    size_t aligned_size = ALIGN8(size);
    if (aligned_size < 16) aligned_size = 16;

    // Search existing free list for first fit
    BlockHeader* curr = g_heap_head;
    while (curr)
    {
        if (curr->is_free && curr->size >= aligned_size)
        {
            // Split block if excess space is large enough
            if (curr->size >= aligned_size + BLOCK_HEADER_SIZE + 16)
            {
                BlockHeader* split = (BlockHeader*)((uint8_t*)curr + BLOCK_HEADER_SIZE + aligned_size);
                split->size = curr->size - aligned_size - BLOCK_HEADER_SIZE;
                split->is_free = true;
                split->next = curr->next;
                split->prev = curr;
                if (curr->next) curr->next->prev = split;
                curr->next = split;
                curr->size = aligned_size;
            }

            curr->is_free = false;
            return (void*)((uint8_t*)curr + BLOCK_HEADER_SIZE);
        }
        curr = curr->next;
    }

    // Need to expand user heap via sys_sbrk
    size_t total_needed = aligned_size + BLOCK_HEADER_SIZE;
    size_t sbrk_chunk = ((total_needed + 4095) / 4096) * 4096; // 4 KiB multiple
    if (sbrk_chunk < 8192) sbrk_chunk = 8192; // Allocate at least 8 KiB at a time

    void* raw = sys_sbrk((int)sbrk_chunk);
    if (raw == (void*)-1 || raw == NULL)
        return NULL;

    BlockHeader* new_block = (BlockHeader*)raw;
    new_block->size = sbrk_chunk - BLOCK_HEADER_SIZE;
    new_block->is_free = true;
    new_block->next = NULL;
    new_block->prev = NULL;

    // Link to tail of heap list
    if (!g_heap_head)
    {
        g_heap_head = new_block;
    }
    else
    {
        BlockHeader* tail = g_heap_head;
        while (tail->next) tail = tail->next;
        tail->next = new_block;
        new_block->prev = tail;
    }

    // Now split the new block if needed
    if (new_block->size >= aligned_size + BLOCK_HEADER_SIZE + 16)
    {
        BlockHeader* split = (BlockHeader*)((uint8_t*)new_block + BLOCK_HEADER_SIZE + aligned_size);
        split->size = new_block->size - aligned_size - BLOCK_HEADER_SIZE;
        split->is_free = true;
        split->next = new_block->next;
        split->prev = new_block;
        if (new_block->next) new_block->next->prev = split;
        new_block->next = split;
        new_block->size = aligned_size;
    }

    new_block->is_free = false;
    return (void*)((uint8_t*)new_block + BLOCK_HEADER_SIZE);
}

void free(void* ptr)
{
    if (!ptr) return;

    BlockHeader* block = (BlockHeader*)((uint8_t*)ptr - BLOCK_HEADER_SIZE);
    block->is_free = true;

    // Forward coalescing
    if (block->next && block->next->is_free)
    {
        block->size += BLOCK_HEADER_SIZE + block->next->size;
        block->next = block->next->next;
        if (block->next) block->next->prev = block;
    }

    // Backward coalescing
    if (block->prev && block->prev->is_free)
    {
        block->prev->size += BLOCK_HEADER_SIZE + block->size;
        block->prev->next = block->next;
        if (block->next) block->next->prev = block->prev;
    }
}

void* calloc(size_t nmemb, size_t size)
{
    size_t total = nmemb * size;
    void* p = malloc(total);
    if (p)
        memset(p, 0, total);
    return p;
}

void* realloc(void* ptr, size_t size)
{
    if (!ptr) return malloc(size);
    if (size == 0)
    {
        free(ptr);
        return NULL;
    }

    BlockHeader* block = (BlockHeader*)((uint8_t*)ptr - BLOCK_HEADER_SIZE);
    size_t aligned_size = ALIGN8(size);

    if (block->size >= aligned_size)
    {
        return ptr; // Already large enough
    }

    // Check if next block is free and can merge
    if (block->next && block->next->is_free &&
        (block->size + BLOCK_HEADER_SIZE + block->next->size >= aligned_size))
    {
        block->size += BLOCK_HEADER_SIZE + block->next->size;
        block->next = block->next->next;
        if (block->next) block->next->prev = block;
        return ptr;
    }

    // Reallocate, copy, and free
    void* new_ptr = malloc(size);
    if (!new_ptr) return NULL;

    size_t copy_size = block->size < size ? block->size : size;
    memcpy(new_ptr, ptr, copy_size);
    free(ptr);
    return new_ptr;
}

// ============================================================================
// Memory & String Manipulation
// ============================================================================

void* memset(void* dest, int c, size_t n)
{
    uint8_t* p = (uint8_t*)dest;
    uint8_t val = (uint8_t)c;
    while (n--) *p++ = val;
    return dest;
}

void* memcpy(void* dest, const void* src, size_t n)
{
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    while (n--) *d++ = *s++;
    return dest;
}

void* memmove(void* dest, const void* src, size_t n)
{
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    if (d < s)
    {
        while (n--) *d++ = *s++;
    }
    else if (d > s)
    {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dest;
}

int memcmp(const void* s1, const void* s2, size_t n)
{
    const uint8_t* p1 = (const uint8_t*)s1;
    const uint8_t* p2 = (const uint8_t*)s2;
    while (n--)
    {
        if (*p1 != *p2)
            return *p1 - *p2;
        p1++;
        p2++;
    }
    return 0;
}

size_t strlen(const char* s)
{
    size_t len = 0;
    while (s && s[len]) len++;
    return len;
}

int strcmp(const char* s1, const char* s2)
{
    while (*s1 && (*s1 == *s2))
    {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

int strncmp(const char* s1, const char* s2, size_t n)
{
    while (n && *s1 && (*s1 == *s2))
    {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

char* strcpy(char* dest, const char* src)
{
    char* orig = dest;
    while ((*dest++ = *src++));
    return orig;
}

char* strncpy(char* dest, const char* src, size_t n)
{
    char* orig = dest;
    while (n && (*dest++ = *src++)) n--;
    while (n--) *dest++ = '\0';
    return orig;
}

char* strcat(char* dest, const char* src)
{
    char* orig = dest;
    while (*dest) dest++;
    while ((*dest++ = *src++));
    return orig;
}

char* strncat(char* dest, const char* src, size_t n)
{
    char* orig = dest;
    while (*dest) dest++;
    while (n-- && (*dest++ = *src++));
    *dest = '\0';
    return orig;
}

char* strchr(const char* s, int c)
{
    char ch = (char)c;
    while (*s)
    {
        if (*s == ch) return (char*)s;
        s++;
    }
    return (ch == '\0') ? (char*)s : NULL;
}

char* strrchr(const char* s, int c)
{
    char ch = (char)c;
    const char* last = NULL;
    while (*s)
    {
        if (*s == ch) last = s;
        s++;
    }
    if (ch == '\0') return (char*)s;
    return (char*)last;
}

char* strstr(const char* haystack, const char* needle)
{
    if (!*needle) return (char*)haystack;
    for (; *haystack; haystack++)
    {
        if (*haystack == *needle)
        {
            const char* h = haystack;
            const char* n = needle;
            while (*h && *n && (*h == *n))
            {
                h++;
                n++;
            }
            if (!*n) return (char*)haystack;
        }
    }
    return NULL;
}

char* strtok(char* str, const char* delim)
{
    static char* last = NULL;
    if (str) last = str;
    if (!last) return NULL;

    // Skip leading delimiters
    while (*last && strchr(delim, *last)) last++;
    if (!*last)
    {
        last = NULL;
        return NULL;
    }

    char* token_start = last;
    while (*last && !strchr(delim, *last)) last++;

    if (*last)
    {
        *last = '\0';
        last++;
    }
    else
    {
        last = NULL;
    }

    return token_start;
}

char* strdup(const char* s)
{
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char* copy = (char*)malloc(len);
    if (copy) memcpy(copy, s, len);
    return copy;
}

// ============================================================================
// Standard I/O Streams (fopen, fclose, fread, fwrite, fseek, ftell)
// ============================================================================

static FILE g_stdin_file  = { .fd = 0, .flags = O_RDONLY, .eof = false, .error = false, .unget_buf = 0, .has_unget = false };
static FILE g_stdout_file = { .fd = 1, .flags = O_WRONLY, .eof = false, .error = false, .unget_buf = 0, .has_unget = false };
static FILE g_stderr_file = { .fd = 2, .flags = O_WRONLY, .eof = false, .error = false, .unget_buf = 0, .has_unget = false };

FILE* stdin  = &g_stdin_file;
FILE* stdout = &g_stdout_file;
FILE* stderr = &g_stderr_file;

FILE* fopen(const char* path, const char* mode)
{
    if (!path || !mode) return NULL;

    int flags = 0;
    if (strcmp(mode, "r") == 0 || strcmp(mode, "rb") == 0)
    {
        flags = O_RDONLY;
    }
    else if (strcmp(mode, "w") == 0 || strcmp(mode, "wb") == 0)
    {
        flags = O_WRONLY | O_CREAT | O_TRUNC;
    }
    else if (strcmp(mode, "a") == 0 || strcmp(mode, "ab") == 0)
    {
        flags = O_WRONLY | O_CREAT | O_APPEND;
    }
    else if (strcmp(mode, "r+") == 0 || strcmp(mode, "r+b") == 0)
    {
        flags = O_RDWR;
    }
    else if (strcmp(mode, "w+") == 0 || strcmp(mode, "w+b") == 0)
    {
        flags = O_RDWR | O_CREAT | O_TRUNC;
    }
    else
    {
        flags = O_RDONLY;
    }

    int fd = sys_open(path, flags);
    if (fd < 0) return NULL;

    FILE* fp = (FILE*)malloc(sizeof(FILE));
    if (!fp)
    {
        sys_close(fd);
        return NULL;
    }

    fp->fd = fd;
    fp->flags = flags;
    fp->eof = false;
    fp->error = false;
    fp->unget_buf = 0;
    fp->has_unget = false;
    return fp;
}

int fclose(FILE* stream)
{
    if (!stream) return EOF;
    fflush(stream);

    int res = 0;
    if (stream != stdin && stream != stdout && stream != stderr)
    {
        res = sys_close(stream->fd);
        free(stream);
    }
    return (res == 0) ? 0 : EOF;
}

size_t fread(void* ptr, size_t size, size_t nmemb, FILE* stream)
{
    if (!ptr || size == 0 || nmemb == 0 || !stream) return 0;

    size_t total_bytes = size * nmemb;
    uint8_t* dst = (uint8_t*)ptr;
    size_t bytes_read = 0;

    if (stream->has_unget)
    {
        *dst++ = (uint8_t)stream->unget_buf;
        stream->has_unget = false;
        bytes_read++;
        total_bytes--;
    }

    if (total_bytes > 0)
    {
        int n = sys_read_file(stream->fd, dst, total_bytes);
        if (n < 0)
        {
            stream->error = true;
        }
        else if (n == 0)
        {
            stream->eof = true;
        }
        else
        {
            bytes_read += (size_t)n;
            if ((size_t)n < total_bytes)
                stream->eof = true;
        }
    }

    return bytes_read / size;
}

size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* stream)
{
    if (!ptr || size == 0 || nmemb == 0 || !stream) return 0;

    size_t total_bytes = size * nmemb;
    int n = sys_write_file(stream->fd, ptr, total_bytes);
    if (n < 0)
    {
        stream->error = true;
        return 0;
    }
    return (size_t)n / size;
}

int fseek(FILE* stream, long offset, int whence)
{
    if (!stream) return -1;
    stream->has_unget = false;
    int res = sys_lseek(stream->fd, (int)offset, whence);
    if (res >= 0)
    {
        stream->eof = false;
        return 0;
    }
    stream->error = true;
    return -1;
}

long ftell(FILE* stream)
{
    if (!stream) return -1;
    return (long)sys_lseek(stream->fd, 0, SEEK_CUR);
}

void rewind(FILE* stream)
{
    fseek(stream, 0, SEEK_SET);
}

int fgetc(FILE* stream)
{
    if (!stream) return EOF;
    if (stream->has_unget)
    {
        stream->has_unget = false;
        return (unsigned char)stream->unget_buf;
    }

    uint8_t ch = 0;
    int n = sys_read_file(stream->fd, &ch, 1);
    if (n <= 0)
    {
        if (n == 0) stream->eof = true;
        else stream->error = true;
        return EOF;
    }
    return (int)ch;
}

int fputc(int c, FILE* stream)
{
    if (!stream) return EOF;
    uint8_t ch = (uint8_t)c;
    int n = sys_write_file(stream->fd, &ch, 1);
    if (n <= 0)
    {
        stream->error = true;
        return EOF;
    }
    return (int)ch;
}

char* fgets(char* s, int size, FILE* stream)
{
    if (!s || size <= 0 || !stream) return NULL;

    int idx = 0;
    while (idx < size - 1)
    {
        int c = fgetc(stream);
        if (c == EOF)
        {
            if (idx == 0) return NULL;
            break;
        }

        s[idx++] = (char)c;
        if (c == '\n') break;
    }

    s[idx] = '\0';
    return s;
}

int fputs(const char* s, FILE* stream)
{
    if (!s || !stream) return EOF;
    size_t len = strlen(s);
    return (fwrite(s, 1, len, stream) == len) ? (int)len : EOF;
}

int fflush(FILE* stream)
{
    (void)stream;
    return 0;
}

int feof(FILE* stream)
{
    return stream ? stream->eof : 1;
}

int ferror(FILE* stream)
{
    return stream ? stream->error : 1;
}

// ============================================================================
// Formatted Output (vsnprintf, snprintf, sprintf, fprintf, printf)
// ============================================================================

void itoa(int val, char* str, int base)
{
    char* p = str;
    char* p1, *p2;
    unsigned long uval = val;
    bool is_neg = false;

    if (base == 10 && val < 0)
    {
        is_neg = true;
        uval = -val;
    }

    if (uval == 0)
    {
        *p++ = '0';
        *p = '\0';
        return;
    }

    while (uval > 0)
    {
        int rem = uval % base;
        *p++ = (rem < 10) ? (rem + '0') : (rem - 10 + 'a');
        uval /= base;
    }

    if (is_neg) *p++ = '-';
    *p = '\0';

    p1 = str;
    p2 = p - 1;
    while (p1 < p2)
    {
        char tmp = *p1;
        *p1++ = *p2;
        *p2-- = tmp;
    }
}

int vsnprintf(char* str, size_t size, const char* fmt, va_list args)
{
    if (!fmt) return 0;
    size_t written = 0;
    char num_buf[32];

    for (const char* p = fmt; *p != '\0'; p++)
    {
        if (*p != '%')
        {
            if (str && written + 1 < size)
                str[written] = *p;
            written++;
            continue;
        }

        p++; // skip '%'
        if (*p == '\0') break;

        bool left_align = false;
        if (*p == '-')
        {
            left_align = true;
            p++;
        }

        bool pad_zero = false;
        if (*p == '0' && !left_align)
        {
            pad_zero = true;
            p++;
        }

        int width = 0;
        while (*p >= '0' && *p <= '9')
        {
            width = width * 10 + (*p - '0');
            p++;
        }

        while (*p == 'l' || *p == 'h')
        {
            p++;
        }

        switch (*p)
        {
            case 's':
            {
                const char* s = va_arg(args, const char*);
                if (!s) s = "(null)";
                int len = (int)strlen(s);
                if (!left_align)
                {
                    while (len < width)
                    {
                        if (str && written + 1 < size) str[written] = ' ';
                        written++;
                        width--;
                    }
                }
                while (*s)
                {
                    if (str && written + 1 < size)
                        str[written] = *s;
                    written++;
                    s++;
                }
                if (left_align)
                {
                    while (len < width)
                    {
                        if (str && written + 1 < size) str[written] = ' ';
                        written++;
                        width--;
                    }
                }
                break;
            }
            case 'd':
            case 'i':
            {
                int val = va_arg(args, int);
                itoa(val, num_buf, 10);
                int len = (int)strlen(num_buf);
                if (!left_align)
                {
                    while (len < width)
                    {
                        char pad = pad_zero ? '0' : ' ';
                        if (str && written + 1 < size)
                            str[written] = pad;
                        written++;
                        width--;
                    }
                }
                for (char* n = num_buf; *n; n++)
                {
                    if (str && written + 1 < size)
                        str[written] = *n;
                    written++;
                }
                if (left_align)
                {
                    while (len < width)
                    {
                        if (str && written + 1 < size) str[written] = ' ';
                        written++;
                        width--;
                    }
                }
                break;
            }
            case 'u':
            {
                unsigned int val = va_arg(args, unsigned int);
                itoa((int)val, num_buf, 10);
                int len = (int)strlen(num_buf);
                if (!left_align)
                {
                    while (len < width)
                    {
                        char pad = pad_zero ? '0' : ' ';
                        if (str && written + 1 < size)
                            str[written] = pad;
                        written++;
                        width--;
                    }
                }
                for (char* n = num_buf; *n; n++)
                {
                    if (str && written + 1 < size)
                        str[written] = *n;
                    written++;
                }
                if (left_align)
                {
                    while (len < width)
                    {
                        if (str && written + 1 < size) str[written] = ' ';
                        written++;
                        width--;
                    }
                }
                break;
            }
            case 'x':
            case 'X':
            {
                unsigned int val = va_arg(args, unsigned int);
                itoa((int)val, num_buf, 16);
                int len = (int)strlen(num_buf);
                while (len < width)
                {
                    char pad = pad_zero ? '0' : ' ';
                    if (str && written + 1 < size)
                        str[written] = pad;
                    written++;
                    width--;
                }
                for (char* n = num_buf; *n; n++)
                {
                    if (str && written + 1 < size)
                        str[written] = *n;
                    written++;
                }
                break;
            }
            case 'c':
            {
                char c = (char)va_arg(args, int);
                if (str && written + 1 < size)
                    str[written] = c;
                written++;
                break;
            }
            case '%':
            {
                if (str && written + 1 < size)
                    str[written] = '%';
                written++;
                break;
            }
            default:
            {
                if (str && written + 1 < size)
                    str[written] = *p;
                written++;
                break;
            }
        }
    }

    if (str && size > 0)
    {
        size_t null_pos = written < size ? written : size - 1;
        str[null_pos] = '\0';
    }

    return (int)written;
}

int snprintf(char* str, size_t size, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int res = vsnprintf(str, size, fmt, args);
    va_end(args);
    return res;
}

int sprintf(char* str, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int res = vsnprintf(str, 0x7FFFFFFF, fmt, args);
    va_end(args);
    return res;
}

int vfprintf(FILE* stream, const char* fmt, va_list args)
{
    char buf[1024];
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    if (n > 0)
    {
        fwrite(buf, 1, (size_t)n, stream);
    }
    return n;
}

int fprintf(FILE* stream, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int res = vfprintf(stream, fmt, args);
    va_end(args);
    return res;
}

int printf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int res = vfprintf(stdout, fmt, args);
    va_end(args);
    return res;
}

// ============================================================================
// Standard Utilities (atoi, atol, rand, srand, qsort, abs)
// ============================================================================

int atoi(const char* s)
{
    return (int)atol(s);
}

long atol(const char* s)
{
    long res = 0;
    int sign = 1;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') { s++; }
    while (*s >= '0' && *s <= '9')
    {
        res = res * 10 + (*s - '0');
        s++;
    }
    return res * sign;
}

int abs(int j)
{
    return (j < 0) ? -j : j;
}

double fabs(double x)
{
    __asm__ ("fabs" : "+t"(x));
    return x;
}

double sqrt(double x)
{
    if (x <= 0.0) return 0.0;
    __asm__ ("fsqrt" : "+t"(x));
    return x;
}

double sin(double x)
{
    __asm__ ("fsin" : "+t"(x));
    return x;
}

double cos(double x)
{
    __asm__ ("fcos" : "+t"(x));
    return x;
}

static unsigned long g_rand_next = 1;

int rand(void)
{
    g_rand_next = g_rand_next * 1103515245 + 12345;
    return (unsigned int)(g_rand_next / 65536) % 32768;
}

void srand(unsigned int seed)
{
    g_rand_next = seed;
}

static void swap_bytes(uint8_t* a, uint8_t* b, size_t size)
{
    while (size--)
    {
        uint8_t tmp = *a;
        *a++ = *b;
        *b++ = tmp;
    }
}

void qsort(void* base, size_t nmemb, size_t size, int (*compar)(const void*, const void*))
{
    if (nmemb < 2 || size == 0 || !base || !compar) return;

    uint8_t* arr = (uint8_t*)base;
    size_t pivot_idx = nmemb - 1;
    uint8_t* pivot = arr + (pivot_idx * size);
    size_t i = 0;

    for (size_t j = 0; j < pivot_idx; j++)
    {
        if (compar(arr + (j * size), pivot) <= 0)
        {
            swap_bytes(arr + (i * size), arr + (j * size), size);
            i++;
        }
    }
    swap_bytes(arr + (i * size), pivot, size);

    qsort(arr, i, size, compar);
    qsort(arr + ((i + 1) * size), nmemb - i - 1, size, compar);
}

// ============================================================================
// Network Subsystem User API
// ============================================================================

int sys_net_info(NetInfoUser* info)
{
    return syscall1(19, (uint32_t)info);
}

int sys_net_ping(uint32_t target_ip, uint32_t timeout_ms)
{
    return syscall2(20, target_ip, timeout_ms);
}

int sys_net_send_udp(uint32_t target_ip, uint16_t src_port, uint16_t dst_port, const void* data, size_t len)
{
    (void)len;
    return syscall3(21, target_ip, ((uint32_t)src_port << 16) | dst_port, (uint32_t)data);
}

void inet_ntoa(uint32_t ip, char* buf)
{
    if (!buf) return;
    uint8_t b1 = ip & 0xFF;
    uint8_t b2 = (ip >> 8) & 0xFF;
    uint8_t b3 = (ip >> 16) & 0xFF;
    uint8_t b4 = (ip >> 24) & 0xFF;
    sprintf(buf, "%u.%u.%u.%u", b1, b2, b3, b4);
}

uint32_t inet_addr(const char* cp)
{
    if (!cp) return 0;
    uint32_t parts[4] = {0, 0, 0, 0};
    int idx = 0;

    while (*cp && idx < 4)
    {
        if (*cp >= '0' && *cp <= '9')
        {
            parts[idx] = parts[idx] * 10 + (*cp - '0');
        }
        else if (*cp == '.')
        {
            idx++;
        }
        cp++;
    }

    return (parts[3] << 24) | (parts[2] << 16) | (parts[1] << 8) | parts[0];
}

// ============================================================================
// BSD Sockets Subsystem User API
// ============================================================================

int sys_socket(int domain, int type, int protocol)
{
    return syscall3(25, domain, type, protocol);
}

int sys_bind(int fd, const struct sockaddr* addr, socklen_t len)
{
    return syscall3(26, fd, (uint32_t)addr, len);
}

int sys_listen(int fd, int backlog)
{
    return syscall2(27, fd, backlog);
}

int sys_connect(int fd, const struct sockaddr* addr, socklen_t len)
{
    return syscall3(28, fd, (uint32_t)addr, len);
}

int sys_accept(int fd, struct sockaddr* addr, socklen_t* len)
{
    return syscall3(29, fd, (uint32_t)addr, (uint32_t)len);
}

int sys_send(int fd, const void* buf, size_t len, int flags)
{
    (void)flags;
    return syscall3(30, fd, (uint32_t)buf, len);
}

int sys_recv(int fd, void* buf, size_t len, int flags)
{
    (void)flags;
    return syscall3(31, fd, (uint32_t)buf, len);
}

int socket(int domain, int type, int protocol)
{
    return sys_socket(domain, type, protocol);
}

int bind(int fd, const struct sockaddr* addr, socklen_t len)
{
    return sys_bind(fd, addr, len);
}

int listen(int fd, int backlog)
{
    return sys_listen(fd, backlog);
}

int connect(int fd, const struct sockaddr* addr, socklen_t len)
{
    return sys_connect(fd, addr, len);
}

int accept(int fd, struct sockaddr* addr, socklen_t* len)
{
    return sys_accept(fd, addr, len);
}

int send(int fd, const void* buf, size_t len, int flags)
{
    return sys_send(fd, buf, len, flags);
}

int recv(int fd, void* buf, size_t len, int flags)
{
    return sys_recv(fd, buf, len, flags);
}

uint16_t htons(uint16_t hostshort)
{
    return (uint16_t)(((hostshort & 0xFF) << 8) | ((hostshort >> 8) & 0xFF));
}

uint16_t ntohs(uint16_t netshort)
{
    return htons(netshort);
}

uint32_t htonl(uint32_t hostlong)
{
    return ((hostlong & 0xFF) << 24) |
           (((hostlong >> 8) & 0xFF) << 16) |
           (((hostlong >> 16) & 0xFF) << 8) |
           ((hostlong >> 24) & 0xFF);
}

uint32_t ntohl(uint32_t netlong)
{
    return htonl(netlong);
}

int sys_kill(pid_t pid, int sig)
{
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(32), "b"(pid), "c"(sig)
        : "memory"
    );
    return ret;
}

sighandler_t sys_signal(int sig, sighandler_t handler, void (*restorer)(void))
{
    sighandler_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(33), "b"(sig), "c"(handler), "d"(restorer)
        : "memory"
    );
    return ret;
}

int sys_sigaction(int sig, const struct sigaction* act, struct sigaction* oldact)
{
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(34), "b"(sig), "c"(act), "d"(oldact)
        : "memory"
    );
    return ret;
}

void sys_sigreturn(void)
{
    __asm__ volatile (
        "int $0x80"
        :
        : "a"(35)
        : "memory"
    );
}

unsigned int sys_alarm(unsigned int seconds)
{
    unsigned int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(36), "b"(seconds)
        : "memory"
    );
    return ret;
}

int sys_pause(void)
{
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(37)
        : "memory"
    );
    return ret;
}

void __attribute__((naked)) __sigreturn_trampoline(void)
{
    __asm__ volatile (
        "mov $35, %eax\n" // SYS_SIGRETURN
        "int $0x80\n"
    );
}

sighandler_t signal(int sig, sighandler_t handler)
{
    return sys_signal(sig, handler, __sigreturn_trampoline);
}

int sigaction(int sig, const struct sigaction* act, struct sigaction* oldact)
{
    struct sigaction real_act;
    if (act)
    {
        real_act = *act;
        if (!real_act.sa_restorer)
            real_act.sa_restorer = __sigreturn_trampoline;
        return sys_sigaction(sig, &real_act, oldact);
    }
    return sys_sigaction(sig, NULL, oldact);
}

int kill(pid_t pid, int sig)
{
    return sys_kill(pid, sig);
}

unsigned int alarm(unsigned int seconds)
{
    return sys_alarm(seconds);
}

int pause(void)
{
    return sys_pause();
}

int sys_gui_create_win(const char* title, int x, int y, int width, int height)
{
    uint32_t dims = ((uint32_t)(width & 0xFFFF) << 16) | (uint32_t)(height & 0xFFFF);
    uint32_t pos  = ((uint32_t)((uint16_t)x) << 16) | (uint32_t)((uint16_t)y);
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_GUI_CREATE_WIN), "b"(title), "c"(dims), "d"(pos)
        : "memory"
    );
    return ret;
}

int sys_gui_destroy_win(int win_id)
{
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_GUI_DESTROY_WIN), "b"(win_id)
        : "memory"
    );
    return ret;
}

int sys_gui_poll_event(int win_id, gui_event_t* ev)
{
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_GUI_POLL_EVENT), "b"(win_id), "c"(ev)
        : "memory"
    );
    return ret;
}

int sys_gui_damage(int win_id, int x, int y, int w, int h)
{
    uint32_t pos  = ((uint32_t)((uint16_t)x) << 16) | (uint32_t)((uint16_t)y);
    uint32_t dims = ((uint32_t)(w & 0xFFFF) << 16) | (uint32_t)(h & 0xFFFF);
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_GUI_DAMAGE), "b"(win_id), "c"(pos), "d"(dims)
        : "memory"
    );
    return ret;
}

void* sys_gui_map_buffer(int win_id)
{
    void* ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_GUI_MAP_BUFFER), "b"(win_id)
        : "memory"
    );
    return ret;
}

int sys_smp_info(smp_info_t* info)
{
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_SMP_INFO), "b"(info)
        : "memory"
    );
    return ret;
}

int sys_audio_play(const void* data, uint32_t len, uint16_t rate, uint8_t bits, uint8_t channels, bool blocking)
{
    audio_play_req_t req;
    req.data = data;
    req.length = len;
    req.sample_rate = rate;
    req.bits = bits;
    req.channels = channels;
    req.blocking = blocking ? 1 : 0;

    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_AUDIO_PLAY), "b"(&req)
        : "memory"
    );
    return ret;
}

int sys_audio_status(audio_status_t* status)
{
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_AUDIO_STATUS), "b"(status)
        : "memory"
    );
    return ret;
}

int sys_audio_stop(void)
{
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_AUDIO_STOP)
        : "memory"
    );
    return ret;
}

int sys_audio_set_vol(uint8_t volume)
{
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_AUDIO_SET_VOL), "b"((uint32_t)volume)
        : "memory"
    );
    return ret;
}

typedef struct {
    char     riff_id[4];     // "RIFF"
    uint32_t riff_size;
    char     wave_id[4];     // "WAVE"
    char     fmt_id[4];      // "fmt "
    uint32_t fmt_size;
    uint16_t audio_format;   // 1 = PCM
    uint16_t num_channels;   // 1 = mono, 2 = stereo
    uint32_t sample_rate;    // e.g. 11025, 22050, 44100
    uint32_t byte_rate;      // sample_rate * num_channels * bits_per_sample / 8
    uint16_t block_align;    // num_channels * bits_per_sample / 8
    uint16_t bits_per_sample;// 8 or 16
    char     data_id[4];     // "data"
    uint32_t data_size;
} __attribute__((packed)) wav_header_t;

int audio_play_wav(const char* filepath, bool blocking)
{
    if (!filepath) return -1;

    int fd = sys_open(filepath, O_RDONLY);
    if (fd < 0) return -1;

    wav_header_t header;
    int bytes_read = sys_read_file(fd, &header, sizeof(wav_header_t));
    if (bytes_read < (int)sizeof(wav_header_t))
    {
        sys_close(fd);
        return -2;
    }

    if (memcmp(header.riff_id, "RIFF", 4) != 0 ||
        memcmp(header.wave_id, "WAVE", 4) != 0)
    {
        sys_close(fd);
        return -3;
    }

    uint32_t pcm_len = header.data_size;
    if (pcm_len == 0 || pcm_len > 131072) // 128 KiB limit
    {
        pcm_len = 8192;
    }

    void* pcm_buf = malloc(pcm_len);
    if (!pcm_buf)
    {
        sys_close(fd);
        return -4;
    }

    int data_read = sys_read_file(fd, pcm_buf, pcm_len);
    sys_close(fd);

    if (data_read <= 0)
    {
        free(pcm_buf);
        return -5;
    }

    int res = sys_audio_play(pcm_buf, data_read, (uint16_t)header.sample_rate,
                             (uint8_t)header.bits_per_sample, (uint8_t)header.num_channels,
                             blocking);

    free(pcm_buf);
    return res;
}
