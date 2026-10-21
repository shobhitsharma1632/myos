#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

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

// Saved register context on user stack during signal delivery
typedef struct {
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t ds;
    uint32_t es;
    uint32_t fs;
    uint32_t gs;
    uint32_t old_mask;
    uint32_t sig;
} __attribute__((packed)) SigContext;

// Signal trampoline frame layout on user stack
typedef struct {
    uint32_t   restorer_return_addr; // Return address to __sigreturn_trampoline
    uint32_t   sig;                  // Argument 1 to handler
    SigContext context;              // Saved context for sigreturn
} __attribute__((packed)) SigFrame;
