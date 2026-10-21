#include "../libuser.h"

static volatile int g_CountUSR1 = 0;
static volatile int g_CountUSR2 = 0;

static void handle_usr1(int sig)
{
    (void)sig;
    g_CountUSR1++;
    printf("myOS trap: Handled SIGUSR1 (total count: %d)\n", g_CountUSR1);
}

static void handle_usr2(int sig)
{
    (void)sig;
    g_CountUSR2++;
    printf("myOS trap: Handled SIGUSR2 (total count: %d)\n", g_CountUSR2);
}

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    pid_t my_pid = sys_getpid();

    printf("myOS trap: Registering signal handlers for PID %u...\n", (unsigned int)my_pid);

    signal(SIGUSR1, handle_usr1);
    signal(SIGUSR2, handle_usr2);

    // Send SIGUSR1 to self
    printf("myOS trap: Sending SIGUSR1 to self...\n");
    kill(my_pid, SIGUSR1);
    sys_yield();

    // Send SIGUSR2 to self
    printf("myOS trap: Sending SIGUSR2 to self...\n");
    kill(my_pid, SIGUSR2);
    sys_yield();

    printf("myOS trap: Results - USR1: %d, USR2: %d\n", g_CountUSR1, g_CountUSR2);

    if (g_CountUSR1 == 1 && g_CountUSR2 == 1)
    {
        printf("myOS trap: Signal delivery and context restoration PASSED!\n");
        return 0;
    }

    printf("myOS trap: Signal count mismatch FAILED!\n");
    return 1;
}
