#include "libuser.h"
#include <stdarg.h>

// Raw syscall dispatchers
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

int sys_spawn(const char* path, int argc, char* const argv[])
{
    return syscall3(16, (uint32_t)path, (uint32_t)argc, (uint32_t)argv);
}

int sys_waitpid(int pid, int* status)
{
    return syscall2(17, (uint32_t)pid, (uint32_t)status);
}

// Utility string functions
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

char* strcat(char* dest, const char* src)
{
    char* orig = dest;
    while (*dest) dest++;
    while ((*dest++ = *src++));
    return orig;
}

int atoi(const char* s)
{
    int res = 0;
    int sign = 1;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') { s++; }
    while (*s >= '0' && *s <= '9')
    {
        res = res * 10 + (*s - '0');
        s++;
    }
    return res * sign;
}

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

    if (is_neg)
        *p++ = '-';

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

int printf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    char buf[32];
    int count = 0;

    for (const char* p = fmt; *p != '\0'; p++)
    {
        if (*p != '%')
        {
            char c[2] = { *p, '\0' };
            sys_print(c);
            count++;
            continue;
        }

        p++; // skip '%'
        switch (*p)
        {
            case 's':
            {
                const char* s = va_arg(args, const char*);
                if (!s) s = "(null)";
                sys_print(s);
                count += strlen(s);
                break;
            }
            case 'd':
            case 'i':
            {
                int val = va_arg(args, int);
                itoa(val, buf, 10);
                sys_print(buf);
                count += strlen(buf);
                break;
            }
            case 'u':
            {
                unsigned int val = va_arg(args, unsigned int);
                itoa(val, buf, 10);
                sys_print(buf);
                count += strlen(buf);
                break;
            }
            case 'x':
            case 'X':
            {
                unsigned int val = va_arg(args, unsigned int);
                itoa(val, buf, 16);
                sys_print(buf);
                count += strlen(buf);
                break;
            }
            case 'c':
            {
                char c = (char)va_arg(args, int);
                char s[2] = { c, '\0' };
                sys_print(s);
                count++;
                break;
            }
            case '%':
            {
                sys_print("%");
                count++;
                break;
            }
            default:
            {
                char s[2] = { *p, '\0' };
                sys_print(s);
                count++;
                break;
            }
        }
    }

    va_end(args);
    return count;
}
