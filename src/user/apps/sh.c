#include "../libuser.h"

#define SH_MAX_LINE 128
#define SH_MAX_ARGS 16
#define SH_MAX_STAGES 8

static int sh_spawn_single(char* cmd_str)
{
    // Skip leading whitespace
    while (*cmd_str == ' ' || *cmd_str == '\t') cmd_str++;
    if (*cmd_str == '\0') return -1;

    // Check for redirection tokens (<, >, >>)
    char* infile = NULL;
    char* outfile = NULL;
    bool append_out = false;

    char* argv[SH_MAX_ARGS];
    int argc = 0;

    char* tok = strtok(cmd_str, " \t\r\n");
    while (tok && argc < SH_MAX_ARGS - 1)
    {
        if (strcmp(tok, "<") == 0)
        {
            infile = strtok(NULL, " \t\r\n");
        }
        else if (strcmp(tok, ">>") == 0)
        {
            outfile = strtok(NULL, " \t\r\n");
            append_out = true;
        }
        else if (strcmp(tok, ">") == 0)
        {
            outfile = strtok(NULL, " \t\r\n");
            append_out = false;
        }
        else
        {
            argv[argc++] = tok;
        }
        tok = strtok(NULL, " \t\r\n");
    }
    argv[argc] = NULL;

    if (argc == 0) return -1;

    // Save standard file descriptors
    int saved_stdin = -1;
    int saved_stdout = -1;

    if (infile)
    {
        int fd_in = sys_open(infile, O_RDONLY);
        if (fd_in < 0)
        {
            fprintf(stderr, "sh: cannot open input file '%s'\n", infile);
            return -1;
        }
        saved_stdin = dup(0);
        dup2(fd_in, 0);
        sys_close(fd_in);
    }

    if (outfile)
    {
        int flags = O_WRONLY | O_CREAT | (append_out ? O_APPEND : O_TRUNC);
        int fd_out = sys_open(outfile, flags);
        if (fd_out < 0)
        {
            fprintf(stderr, "sh: cannot open output file '%s'\n", outfile);
            if (saved_stdin >= 0) { dup2(saved_stdin, 0); sys_close(saved_stdin); }
            return -1;
        }
        saved_stdout = dup(1);
        dup2(fd_out, 1);
        sys_close(fd_out);
    }

    const char* cmd = argv[0];
    int child_pid = -1;

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
        printf("  cmd1 | cmd2     - Pipeline execution\n");
        printf("  cmd > file      - Output redirection (truncate)\n");
        printf("  cmd >> file     - Output redirection (append)\n");
        printf("  cmd < file      - Input redirection\n");
        printf("  <program> [args]- Execute disk binary from /bin/\n");
    }
    else
    {
        char tryPath[64];
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

        if (child_pid < 0)
        {
            fprintf(stderr, "sh: command not found: %s\n", cmd);
        }
    }

    // Restore standard descriptors
    if (saved_stdin >= 0)
    {
        dup2(saved_stdin, 0);
        sys_close(saved_stdin);
    }
    if (saved_stdout >= 0)
    {
        dup2(saved_stdout, 1);
        sys_close(saved_stdout);
    }

    return child_pid;
}

static void sh_execute_cmd(char* line)
{
    // Skip leading whitespace
    while (*line == ' ' || *line == '\t') line++;
    if (*line == '\0') return;

    // Check if line contains pipe '|'
    char* pipe_pos = strchr(line, '|');
    if (!pipe_pos)
    {
        // Simple command without pipes
        int pid = sh_spawn_single(line);
        if (pid > 0)
        {
            int status = -1;
            sys_waitpid(pid, &status);
        }
        return;
    }

    // Split pipeline into stages
    char* stages[SH_MAX_STAGES];
    int num_stages = 0;

    char* curr = line;
    while (curr && num_stages < SH_MAX_STAGES)
    {
        stages[num_stages++] = curr;
        char* next = strchr(curr, '|');
        if (next)
        {
            *next = '\0';
            curr = next + 1;
        }
        else
        {
            break;
        }
    }

    // Execute pipeline with pipes
    int prev_read_fd = -1;
    int pids[SH_MAX_STAGES];

    for (int i = 0; i < num_stages; i++)
    {
        int next_pfd[2] = { -1, -1 };
        if (i < num_stages - 1)
        {
            if (pipe(next_pfd) < 0)
            {
                fprintf(stderr, "sh: failed to create pipe\n");
                return;
            }
        }

        int saved_in = dup(0);
        int saved_out = dup(1);

        if (prev_read_fd >= 0)
        {
            dup2(prev_read_fd, 0);
            sys_close(prev_read_fd);
            prev_read_fd = -1;
        }

        if (next_pfd[1] >= 0)
        {
            dup2(next_pfd[1], 1);
            sys_close(next_pfd[1]);
        }

        pids[i] = sh_spawn_single(stages[i]);

        dup2(saved_in, 0);
        sys_close(saved_in);
        dup2(saved_out, 1);
        sys_close(saved_out);

        if (next_pfd[0] >= 0)
        {
            prev_read_fd = next_pfd[0];
        }
    }

    if (prev_read_fd >= 0)
    {
        sys_close(prev_read_fd);
    }

    // Wait for all child processes in the pipeline
    for (int i = 0; i < num_stages; i++)
    {
        if (pids[i] > 0)
        {
            int status = -1;
            sys_waitpid(pids[i], &status);
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
