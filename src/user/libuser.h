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

typedef int32_t pid_t;

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
int      sys_pipe(int pipefd[2]);
int      sys_dup(int oldfd);
int      sys_dup2(int oldfd, int newfd);

// POSIX Pipe & File Descriptor Aliases
int      pipe(int pipefd[2]);
int      dup(int oldfd);
int      dup2(int oldfd, int newfd);

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

// BSD Sockets Subsystem API
#define AF_UNSPEC 0
#define AF_UNIX   1
#define AF_INET   2

#define SOCK_STREAM 1
#define SOCK_DGRAM  2
#define SOCK_RAW    3

#define IPPROTO_IP   0
#define IPPROTO_ICMP 1
#define IPPROTO_TCP  6
#define IPPROTO_UDP  17

#define INADDR_ANY       0x00000000
#define INADDR_LOOPBACK  0x7F000001
#define INADDR_BROADCAST 0xFFFFFFFF

typedef uint32_t socklen_t;

struct in_addr {
    uint32_t s_addr;
};

struct sockaddr {
    uint16_t sa_family;
    char     sa_data[14];
};

struct sockaddr_in {
    uint16_t       sin_family;
    uint16_t       sin_port;
    struct in_addr sin_addr;
    char           sin_zero[8];
};

// Low-level socket syscall wrappers
int      sys_socket(int domain, int type, int protocol);
int      sys_bind(int fd, const struct sockaddr* addr, socklen_t len);
int      sys_listen(int fd, int backlog);
int      sys_connect(int fd, const struct sockaddr* addr, socklen_t len);
int      sys_accept(int fd, struct sockaddr* addr, socklen_t* len);
int      sys_send(int fd, const void* buf, size_t len, int flags);
int      sys_recv(int fd, void* buf, size_t len, int flags);

// Standard BSD Socket API aliases
int      socket(int domain, int type, int protocol);
int      bind(int fd, const struct sockaddr* addr, socklen_t len);
int      listen(int fd, int backlog);
int      connect(int fd, const struct sockaddr* addr, socklen_t len);
int      accept(int fd, struct sockaddr* addr, socklen_t* len);
int      send(int fd, const void* buf, size_t len, int flags);
int      recv(int fd, void* buf, size_t len, int flags);

// Endian byte order conversion
uint16_t htons(uint16_t hostshort);
uint16_t ntohs(uint16_t netshort);
uint32_t htonl(uint32_t hostlong);
uint32_t ntohl(uint32_t netlong);

// POSIX Signal Constants
#define SIGHUP    1
#define SIGINT    2
#define SIGQUIT   3
#define SIGILL    4
#define SIGTRAP   5
#define SIGABRT   6
#define SIGBUS    7
#define SIGFPE    8
#define SIGKILL   9
#define SIGUSR1   10
#define SIGSEGV   11
#define SIGUSR2   12
#define SIGPIPE   13
#define SIGALRM   14
#define SIGTERM   15
#define SIGCHLD   17
#define SIGCONT   18
#define SIGSTOP   19
#define SIGTSTP   20
#define NSIG      32

typedef void (*sighandler_t)(int);
#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)

typedef uint32_t sigset_t;

struct sigaction {
    sighandler_t sa_handler;
    sigset_t     sa_mask;
    int          sa_flags;
    void       (*sa_restorer)(void);
};

// POSIX Signal Syscall Wrappers & Standard APIs
int          sys_kill(pid_t pid, int sig);
sighandler_t sys_signal(int sig, sighandler_t handler, void (*restorer)(void));
int          sys_sigaction(int sig, const struct sigaction* act, struct sigaction* oldact);
void         sys_sigreturn(void);
unsigned int sys_alarm(unsigned int seconds);
int          sys_pause(void);

sighandler_t signal(int sig, sighandler_t handler);
int          sigaction(int sig, const struct sigaction* act, struct sigaction* oldact);
int          kill(pid_t pid, int sig);
unsigned int alarm(unsigned int seconds);
int          pause(void);
void         __sigreturn_trampoline(void);
