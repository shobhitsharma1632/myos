#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// VFS Flags
#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_CREAT     0x0040
#define O_TRUNC     0x0200
#define O_APPEND    0x0400

// Syscall API
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
int      sys_spawn(const char* path, int argc, char* const argv[]);
int      sys_waitpid(int pid, int* status);

// Utility functions
size_t   strlen(const char* s);
int      strcmp(const char* s1, const char* s2);
int      strncmp(const char* s1, const char* s2, size_t n);
char*    strcpy(char* dest, const char* src);
char*    strcat(char* dest, const char* src);
int      atoi(const char* s);
void     itoa(int val, char* str, int base);
int      printf(const char* fmt, ...);
