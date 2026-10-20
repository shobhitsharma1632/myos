#include "../libuser.h"

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: grep <pattern> [filepath]\n");
        return 1;
    }

    const char* pattern = argv[1];
    FILE* fp = stdin;
    const char* path = "(stdin)";
    bool is_stdin = (argc < 3 || strcmp(argv[2], "-") == 0);

    if (!is_stdin)
    {
        path = argv[2];
        fp = fopen(path, "r");
        if (!fp)
        {
            fprintf(stderr, "grep: cannot open '%s'\n", path);
            return 1;
        }
    }

    char line[256];
    int line_num = 1;
    int matches = 0;

    while (fgets(line, sizeof(line), fp))
    {
        if (strstr(line, pattern))
        {
            if (is_stdin)
            {
                printf("%s", line);
            }
            else
            {
                printf("[%s:%d] %s", path, line_num, line);
            }

            size_t len = strlen(line);
            if (len > 0 && line[len - 1] != '\n')
                printf("\n");

            matches++;
        }
        line_num++;
    }

    if (!is_stdin)
    {
        fclose(fp);
    }

    return (matches > 0) ? 0 : 1;
}
