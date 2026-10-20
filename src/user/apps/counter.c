#include "../libuser.h"

int main(int argc, char* argv[])
{
    printf("\n[User Counter] Counter process active (PID %d)\n", sys_getpid());

    int count = 5;
    if (argc >= 2)
    {
        count = atoi(argv[1]);
        if (count <= 0 || count > 20) count = 5;
    }

    for (int i = 1; i <= count; i++)
    {
        printf("[User Counter] Iteration %d/%d (sleeping 10 ticks)...\n", i, count);
        sys_sleep(10);
    }

    printf("[User Counter] Completed all %d sleep cycles! Exiting with code 42.\n\n", count);
    return 42;
}
