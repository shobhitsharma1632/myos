#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>

#ifndef NULL
#define NULL ((void*)0)
#endif

#define EOF (-1)

// Standard Stream Seeking Constants
#define SEEK_SET    0
#define SEEK_CUR    1
#define SEEK_END    2

// POSIX VFS File Open Flags
#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_CREAT     0x0040
#define O_TRUNC     0x0200
#define O_APPEND    0x0400

// Standard Exit Codes
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

// File Stream Abstraction
typedef struct FILE FILE;

struct FILE {
    int     fd;
    int     flags;
    bool    eof;
    bool    error;
    char    unget_buf;
    bool    has_unget;
};

extern FILE* stdin;
extern FILE* stdout;
extern FILE* stderr;

// Standard I/O Functions
FILE*   fopen(const char* path, const char* mode);
int     fclose(FILE* stream);
size_t  fread(void* ptr, size_t size, size_t nmemb, FILE* stream);
size_t  fwrite(const void* ptr, size_t size, size_t nmemb, FILE* stream);
int     fseek(FILE* stream, long offset, int whence);
long    ftell(FILE* stream);
void    rewind(FILE* stream);
int     fgetc(FILE* stream);
int     fputc(int c, FILE* stream);
char*   fgets(char* s, int size, FILE* stream);
int     fputs(const char* s, FILE* stream);
int     fflush(FILE* stream);
int     feof(FILE* stream);
int     ferror(FILE* stream);

// Formatted Output Functions
int     printf(const char* fmt, ...);
int     fprintf(FILE* stream, const char* fmt, ...);
int     vfprintf(FILE* stream, const char* fmt, va_list args);
int     sprintf(char* str, const char* fmt, ...);
int     snprintf(char* str, size_t size, const char* fmt, ...);
int     vsnprintf(char* str, size_t size, const char* fmt, va_list args);

// Dynamic Memory Management (User Heap)
void*   malloc(size_t size);
void    free(void* ptr);
void*   calloc(size_t nmemb, size_t size);
void*   realloc(void* ptr, size_t size);

// Memory & String Manipulation
void*   memset(void* dest, int c, size_t n);
void*   memcpy(void* dest, const void* src, size_t n);
void*   memmove(void* dest, const void* src, size_t n);
int     memcmp(const void* s1, const void* s2, size_t n);

size_t  strlen(const char* s);
int     strcmp(const char* s1, const char* s2);
int     strncmp(const char* s1, const char* s2, size_t n);
char*   strcpy(char* dest, const char* src);
char*   strncpy(char* dest, const char* src, size_t n);
char*   strcat(char* dest, const char* src);
char*   strncat(char* dest, const char* src, size_t n);
char*   strchr(const char* s, int c);
char*   strrchr(const char* s, int c);
char*   strstr(const char* haystack, const char* needle);
char*   strtok(char* str, const char* delim);
char*   strdup(const char* s);

// General Standard Utilities
int     atoi(const char* s);
long    atol(const char* s);
void    itoa(int val, char* str, int base);
void    qsort(void* base, size_t nmemb, size_t size, int (*compar)(const void*, const void*));
int     rand(void);
void    srand(unsigned int seed);
int     abs(int j);

// Low-Level Kernel Syscall Wrappers
void     sys_exit(int code) __attribute__((noreturn));
void     sys_print(const char* str);
void     sys_println(const char* str);
int      sys_read(char* buf, int max_len);
void     sys_yield(void);
int      sys_getpid(void);
void     sys_sleep(uint32_t ticks);
uint32_t sys_getticks(void);
void*    sys_sbrk(int incr);
int      sys_open(const char* path, int flags);
int      sys_close(int fd);
int      sys_read_file(int fd, void* buf, size_t count);
int      sys_write_file(int fd, const void* buf, size_t count);
int      sys_lseek(int fd, int offset, int whence);
int      sys_spawn(const char* path, int argc, char* const argv[]);
int      sys_waitpid(int pid, int* status);

// Network Subsystem API
typedef struct {
    uint8_t  mac[6];
    uint32_t ip;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns;
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t rx_bytes;
    uint32_t tx_bytes;
} NetInfoUser;

int      sys_net_info(NetInfoUser* info);
int      sys_net_ping(uint32_t target_ip, uint32_t timeout_ms);
int      sys_net_send_udp(uint32_t target_ip, uint16_t src_port, uint16_t dst_port, const void* data, size_t len);
void     inet_ntoa(uint32_t ip, char* buf);
uint32_t inet_addr(const char* cp);
