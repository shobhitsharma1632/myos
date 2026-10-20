#include "stdio.h"
#include <arch/i686/io.h>
#include <arch/i686/vga_text.h>

#include <stdarg.h>
#include <stdbool.h>
#include <hal/vfs.h>

void fputc(char c, fd_t file)
{
    VFS_Write(file, (uint8_t*)&c, sizeof(c));
}

void fputs(const char* str, fd_t file)
{
    while (*str)
    {
        fputc(*str, file);
        str++;
    }
}

#define PRINTF_STATE_NORMAL         0
#define PRINTF_STATE_LENGTH         1
#define PRINTF_STATE_LENGTH_SHORT   2
#define PRINTF_STATE_LENGTH_LONG    3
#define PRINTF_STATE_SPEC           4

#define PRINTF_LENGTH_DEFAULT       0
#define PRINTF_LENGTH_SHORT_SHORT   1
#define PRINTF_LENGTH_SHORT         2
#define PRINTF_LENGTH_LONG          3
#define PRINTF_LENGTH_LONG_LONG     4

const char g_HexChars[] = "0123456789abcdef";

void fprintf_unsigned(fd_t file, unsigned long long number, int radix, int width, bool pad_zero)
{
    char buffer[32];
    int pos = 0;

    // convert number to ASCII
    do 
    {
        unsigned long long rem = number % radix;
        number /= radix;
        buffer[pos++] = g_HexChars[rem];
    } while (number > 0);

    while (pos < width && pos < 31)
    {
        buffer[pos++] = pad_zero ? '0' : ' ';
    }

    // print number in reverse order
    while (--pos >= 0)
        fputc(buffer[pos], file);
}

void fprintf_signed(fd_t file, long long number, int radix, int width, bool pad_zero)
{
    if (number < 0)
    {
        fputc('-', file);
        if (width > 0) width--;
        fprintf_unsigned(file, -number, radix, width, pad_zero);
    }
    else fprintf_unsigned(file, number, radix, width, pad_zero);
}

void vfprintf(fd_t file, const char* fmt, va_list args)
{
    int state = PRINTF_STATE_NORMAL;
    int length = PRINTF_LENGTH_DEFAULT;
    int radix = 10;
    bool sign = false;
    bool number = false;
    int width = 0;
    bool pad_zero = false;

    while (*fmt)
    {
        switch (state)
        {
            case PRINTF_STATE_NORMAL:
                switch (*fmt)
                {
                    case '%':   state = PRINTF_STATE_LENGTH;
                                width = 0;
                                pad_zero = false;
                                break;
                    default:    fputc(*fmt, file);
                                break;
                }
                break;

            case PRINTF_STATE_LENGTH:
                if (*fmt == '0' && width == 0)
                {
                    pad_zero = true;
                    break;
                }
                else if (*fmt >= '0' && *fmt <= '9')
                {
                    width = width * 10 + (*fmt - '0');
                    break;
                }
                switch (*fmt)
                {
                    case 'h':   length = PRINTF_LENGTH_SHORT;
                                state = PRINTF_STATE_LENGTH_SHORT;
                                break;
                    case 'l':   length = PRINTF_LENGTH_LONG;
                                state = PRINTF_STATE_LENGTH_LONG;
                                break;
                    default:    goto PRINTF_STATE_SPEC_;
                }
                break;

            case PRINTF_STATE_LENGTH_SHORT:
                if (*fmt == 'h')
                {
                    length = PRINTF_LENGTH_SHORT_SHORT;
                    state = PRINTF_STATE_SPEC;
                }
                else goto PRINTF_STATE_SPEC_;
                break;

            case PRINTF_STATE_LENGTH_LONG:
                if (*fmt == 'l')
                {
                    length = PRINTF_LENGTH_LONG_LONG;
                    state = PRINTF_STATE_SPEC;
                }
                else goto PRINTF_STATE_SPEC_;
                break;

            case PRINTF_STATE_SPEC:
            PRINTF_STATE_SPEC_:
                switch (*fmt)
                {
                    case 'c':   fputc((char)va_arg(args, int), file);
                                break;

                    case 's':   
                                fputs(va_arg(args, const char*), file);
                                break;

                    case '%':   fputc('%', file);
                                break;

                    case 'd':
                    case 'i':   radix = 10; sign = true; number = true;
                                break;

                    case 'u':   radix = 10; sign = false; number = true;
                                break;

                    case 'X':
                    case 'x':
                    case 'p':   radix = 16; sign = false; number = true;
                                break;

                    case 'o':   radix = 8; sign = false; number = true;
                                break;

                    // ignore invalid spec
                    default:    break;
                }

                if (number)
                {
                    if (sign)
                    {
                        switch (length)
                        {
                        case PRINTF_LENGTH_SHORT_SHORT:
                        case PRINTF_LENGTH_SHORT:
                        case PRINTF_LENGTH_DEFAULT:     fprintf_signed(file, va_arg(args, int), radix, width, pad_zero);
                                                        break;

                        case PRINTF_LENGTH_LONG:        fprintf_signed(file, va_arg(args, long), radix, width, pad_zero);
                                                        break;

                        case PRINTF_LENGTH_LONG_LONG:   fprintf_signed(file, va_arg(args, long long), radix, width, pad_zero);
                                                        break;
                        }
                    }
                    else
                    {
                        switch (length)
                        {
                        case PRINTF_LENGTH_SHORT_SHORT:
                        case PRINTF_LENGTH_SHORT:
                        case PRINTF_LENGTH_DEFAULT:     fprintf_unsigned(file, va_arg(args, unsigned int), radix, width, pad_zero);
                                                        break;
                                                        
                        case PRINTF_LENGTH_LONG:        fprintf_unsigned(file, va_arg(args, unsigned long), radix, width, pad_zero);
                                                        break;

                        case PRINTF_LENGTH_LONG_LONG:   fprintf_unsigned(file, va_arg(args, unsigned long long), radix, width, pad_zero);
                                                        break;
                        }
                    }
                }

                // reset state
                state = PRINTF_STATE_NORMAL;
                length = PRINTF_LENGTH_DEFAULT;
                radix = 10;
                sign = false;
                number = false;
                width = 0;
                pad_zero = false;
                break;
        }

        fmt++;
    }
}

void fprintf(fd_t file, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vfprintf(file, fmt, args);
    va_end(args);
}

void fprint_buffer(fd_t file, const char* msg, const void* buffer, uint32_t count)
{
    const uint8_t* u8Buffer = (const uint8_t*)buffer;
    
    fputs(msg, file);
    for (uint16_t i = 0; i < count; i++)
    {
        fputc(g_HexChars[u8Buffer[i] >> 4], file);
        fputc(g_HexChars[u8Buffer[i] & 0xF], file);
    }
    fputs("\n", file);
}

void putc(char c)
{
    fputc(c, VFS_FD_STDOUT);
}

void puts(const char* str)
{
    fputs(str, VFS_FD_STDOUT);
}

void printf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vfprintf(VFS_FD_STDOUT, fmt, args);
    va_end(args);
}

void print_buffer(const char* msg, const void* buffer, uint32_t count)
{
    fprint_buffer(VFS_FD_STDOUT, msg, buffer, count);
}

void debugc(char c)
{
    fputc(c, VFS_FD_DEBUG);
}

void debugs(const char* str)
{
    fputs(str, VFS_FD_DEBUG);
}

void debugf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vfprintf(VFS_FD_DEBUG, fmt, args);
    va_end(args);
}

void debug_buffer(const char* msg, const void* buffer, uint32_t count)
{
    fprint_buffer(VFS_FD_DEBUG, msg, buffer, count);
}

void clrscr()
{
    VGA_clrscr();
}

typedef struct {
    char* buffer;
    size_t size;
    size_t written;
} SnprintfContext;

static void snprintf_putc(char c, SnprintfContext* ctx)
{
    if (ctx->buffer && ctx->size > 0 && ctx->written + 1 < ctx->size)
    {
        ctx->buffer[ctx->written] = c;
    }
    ctx->written++;
}

static void snprintf_puts(const char* s, SnprintfContext* ctx)
{
    while (s && *s)
    {
        snprintf_putc(*s++, ctx);
    }
}

static void snprintf_unsigned(unsigned long long number, int radix, int width, bool pad_zero, SnprintfContext* ctx)
{
    char buf[32];
    int pos = 0;
    do {
        unsigned long long rem = number % radix;
        number /= radix;
        buf[pos++] = g_HexChars[rem];
    } while (number > 0);

    while (pos < width && pos < 31)
    {
        buf[pos++] = pad_zero ? '0' : ' ';
    }

    while (--pos >= 0)
        snprintf_putc(buf[pos], ctx);
}

static void snprintf_signed(long long number, int radix, int width, bool pad_zero, SnprintfContext* ctx)
{
    if (number < 0)
    {
        snprintf_putc('-', ctx);
        if (width > 0) width--;
        snprintf_unsigned(-number, radix, width, pad_zero, ctx);
    }
    else
    {
        snprintf_unsigned(number, radix, width, pad_zero, ctx);
    }
}

int vsnprintf(char* buffer, size_t size, const char* fmt, va_list args)
{
    SnprintfContext ctx = { buffer, size, 0 };
    int state = PRINTF_STATE_NORMAL;
    int length = PRINTF_LENGTH_DEFAULT;
    int radix = 10;
    bool sign = false;
    bool number = false;
    int width = 0;
    bool pad_zero = false;

    while (*fmt)
    {
        switch (state)
        {
            case PRINTF_STATE_NORMAL:
                switch (*fmt)
                {
                    case '%': state = PRINTF_STATE_LENGTH; width = 0; pad_zero = false; break;
                    default:  snprintf_putc(*fmt, &ctx); break;
                }
                break;

            case PRINTF_STATE_LENGTH:
                if (*fmt == '0' && width == 0)
                {
                    pad_zero = true;
                    break;
                }
                else if (*fmt >= '0' && *fmt <= '9')
                {
                    width = width * 10 + (*fmt - '0');
                    break;
                }
                switch (*fmt)
                {
                    case 'h': length = PRINTF_LENGTH_SHORT; state = PRINTF_STATE_LENGTH_SHORT; break;
                    case 'l': length = PRINTF_LENGTH_LONG;  state = PRINTF_STATE_LENGTH_LONG; break;
                    default:  goto SNPRINTF_STATE_SPEC_;
                }
                break;

            case PRINTF_STATE_LENGTH_SHORT:
                if (*fmt == 'h')
                {
                    length = PRINTF_LENGTH_SHORT_SHORT;
                    state = PRINTF_STATE_SPEC;
                }
                else goto SNPRINTF_STATE_SPEC_;
                break;

            case PRINTF_STATE_LENGTH_LONG:
                if (*fmt == 'l')
                {
                    length = PRINTF_LENGTH_LONG_LONG;
                    state = PRINTF_STATE_SPEC;
                }
                else goto SNPRINTF_STATE_SPEC_;
                break;

            case PRINTF_STATE_SPEC:
            SNPRINTF_STATE_SPEC_:
                switch (*fmt)
                {
                    case 'c': snprintf_putc((char)va_arg(args, int), &ctx); break;
                    case 's': snprintf_puts(va_arg(args, const char*), &ctx); break;
                    case '%': snprintf_putc('%', &ctx); break;
                    case 'd':
                    case 'i': radix = 10; sign = true; number = true; break;
                    case 'u': radix = 10; sign = false; number = true; break;
                    case 'X':
                    case 'x':
                    case 'p': radix = 16; sign = false; number = true; break;
                    case 'o': radix = 8; sign = false; number = true; break;
                    default:  break;
                }

                if (number)
                {
                    if (sign)
                    {
                        switch (length)
                        {
                            case PRINTF_LENGTH_SHORT_SHORT:
                            case PRINTF_LENGTH_SHORT:
                            case PRINTF_LENGTH_DEFAULT:
                                snprintf_signed(va_arg(args, int), radix, width, pad_zero, &ctx);
                                break;
                            case PRINTF_LENGTH_LONG:
                                snprintf_signed(va_arg(args, long), radix, width, pad_zero, &ctx);
                                break;
                            case PRINTF_LENGTH_LONG_LONG:
                                snprintf_signed(va_arg(args, long long), radix, width, pad_zero, &ctx);
                                break;
                        }
                    }
                    else
                    {
                        switch (length)
                        {
                            case PRINTF_LENGTH_SHORT_SHORT:
                            case PRINTF_LENGTH_SHORT:
                            case PRINTF_LENGTH_DEFAULT:
                                snprintf_unsigned(va_arg(args, unsigned int), radix, width, pad_zero, &ctx);
                                break;
                            case PRINTF_LENGTH_LONG:
                                snprintf_unsigned(va_arg(args, unsigned long), radix, width, pad_zero, &ctx);
                                break;
                            case PRINTF_LENGTH_LONG_LONG:
                                snprintf_unsigned(va_arg(args, unsigned long long), radix, width, pad_zero, &ctx);
                                break;
                        }
                    }
                }

                state = PRINTF_STATE_NORMAL;
                length = PRINTF_LENGTH_DEFAULT;
                radix = 10;
                sign = false;
                number = false;
                width = 0;
                pad_zero = false;
                break;
        }
        fmt++;
    }

    if (buffer && size > 0)
    {
        if (ctx.written < size)
            buffer[ctx.written] = '\0';
        else
            buffer[size - 1] = '\0';
    }

    return (int)ctx.written;
}

int snprintf(char* buffer, size_t size, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int res = vsnprintf(buffer, size, fmt, args);
    va_end(args);
    return res;
}

int sprintf(char* buffer, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int res = vsnprintf(buffer, (size_t)-1, fmt, args);
    va_end(args);
    return res;
}