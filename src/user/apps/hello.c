#include "../libuser.h"

int main(int argc, char* argv[])
{
    printf("\n[User App] ==============================================\n");
    printf("[User App] Hello from standalone ELF binary '/bin/hello.elf'!\n");
    printf("[User App] Process ID (PID): %d | Execution Level: Ring 3\n", sys_getpid());
    printf("[User App] Received %d argument(s):\n", argc);

    for (int i = 0; i < argc; i++)
    {
        printf("  argv[%d] = '%s'\n", i, argv[i]);
    }

    printf("[User App] Exiting cleanly with code 0.\n");
    printf("[User App] ==============================================\n\n");
    return 0;
}
