#include "../libuser.h"

int main(int argc, char* argv[])
{
    int fd = 0; // default to stdin
    bool is_stdin = (argc < 2 || strcmp(argv[1], "-") == 0);

    if (!is_stdin)
    {
        const char* filepath = argv[1];
        fd = sys_open(filepath, O_RDONLY);
        if (fd < 0)
        {
            fprintf(stderr, "cat: cannot open '%s'\n", filepath);
            return 1;
        }
    }

    char buffer[128];
    int n;

    while ((n = sys_read_file(fd, buffer, sizeof(buffer) - 1)) > 0)
    {
        buffer[n] = '\0';
        sys_print(buffer);
    }

    if (!is_stdin)
    {
        sys_close(fd);
    }

    return 0;
}
