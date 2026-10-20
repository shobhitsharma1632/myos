#include "../libuser.h"

#define SH_MAX_LINE 128
#define SH_MAX_ARGS 16

static void sh_execute_cmd(char* line)
{
    // Skip leading whitespace
    while (*line == ' ' || *line == '\t') line++;
    if (*line == '\0') return;

    char* argv[SH_MAX_ARGS];
    int argc = 0;

    char* tok = strtok(line, " \t\r\n");
    while (tok && argc < SH_MAX_ARGS - 1)
    {
        argv[argc++] = tok;
        tok = strtok(NULL, " \t\r\n");
    }
    argv[argc] = NULL;

    if (argc == 0) return;

    const char* cmd = argv[0];

    if (strcmp(cmd, "exit") == 0)
    {
        int code = (argc > 1) ? atoi(argv[1]) : 0;
        sys_exit(code);
    }
    else if (strcmp(cmd, "echo") == 0)
    {
        for (int i = 1; i < argc; i++)
        {
            printf("%s%s", argv[i], (i + 1 < argc) ? " " : "");
        }
        printf("\n");
    }
    else if (strcmp(cmd, "pid") == 0)
    {
        printf("Shell PID: %d (Ring 3 User Mode)\n", sys_getpid());
    }
    else if (strcmp(cmd, "help") == 0)
    {
        printf("myOS Ring 3 User Shell built-ins:\n");
        printf("  help            - Display help\n");
        printf("  echo <text..>   - Print text\n");
        printf("  pid             - Print shell process ID\n");
        printf("  exit [code]     - Exit shell\n");
        printf("  <program> [args]- Execute disk binary from /bin/\n");
    }
    else
    {
        // Try exact path, then /bin/<cmd>, then /bin/<cmd>.elf
        char tryPath[64];
        int child_pid = -1;

        if (cmd[0] == '/')
        {
            strcpy(tryPath, cmd);
            child_pid = sys_spawn(tryPath, argc, argv);
        }
        else
        {
            strcpy(tryPath, "/bin/");
            strcat(tryPath, cmd);
            child_pid = sys_spawn(tryPath, argc, argv);
            if (child_pid < 0)
            {
                strcat(tryPath, ".elf");
                child_pid = sys_spawn(tryPath, argc, argv);
            }
        }

        if (child_pid > 0)
        {
            int status = -1;
            sys_waitpid(child_pid, &status);
            printf("[Process %d exited with code %d]\n", child_pid, status);
        }
        else
        {
            printf("sh: command not found: %s\n", cmd);
        }
    }
}

int main(int argc, char* argv[])
{
    printf("\n=== myOS User-Space Shell (Ring 3) ===\n");
    printf("PID: %d | Type 'help' for commands, 'exit' to quit.\n\n", sys_getpid());

    // If invoked with -c "<cmd>", execute single command and exit
    if (argc >= 3 && strcmp(argv[1], "-c") == 0)
    {
        sh_execute_cmd(argv[2]);
        return 0;
    }

    char line[SH_MAX_LINE];
    while (true)
    {
        printf("user$ ");
        int n = sys_read(line, sizeof(line) - 1);
        if (n <= 0) break;
        line[n] = '\0';

        // Strip trailing newline
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
        {
            line[n - 1] = '\0';
            n--;
        }

        sh_execute_cmd(line);
    }

    return 0;
}
