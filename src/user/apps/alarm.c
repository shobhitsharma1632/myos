#include "../libuser.h"

static volatile int g_AlarmTriggered = 0;

static void handle_alarm(int sig)
{
    (void)sig;
    g_AlarmTriggered = 1;
    printf("myOS alarm: Caught SIGALRM (signal %d) successfully!\n", sig);
}

int main(int argc, char* argv[])
{
    unsigned int secs = 1;
    if (argc > 1)
    {
        secs = (unsigned int)atoi(argv[1]);
        if (secs == 0) secs = 1;
    }

    printf("myOS alarm: Setting alarm for %u second(s) (PID %u)...\n", secs, (unsigned int)sys_getpid());

    signal(SIGALRM, handle_alarm);
    alarm(secs);

    printf("myOS alarm: Waiting for SIGALRM via pause()...\n");
    pause();

    if (g_AlarmTriggered)
    {
        printf("myOS alarm: Resumed from pause()! Alarm verification PASSED.\n");
        return 0;
    }
    else
    {
        printf("myOS alarm: Woke up but alarm flag was not set!\n");
        return 1;
    }
}
