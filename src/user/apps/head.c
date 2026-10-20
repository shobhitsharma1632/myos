#include "../libuser.h"

int main(int argc, char* argv[])
{
    int max_lines = 10;
    const char* filepath = NULL;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc)
        {
            max_lines = atoi(argv[++i]);
            if (max_lines <= 0) max_lines = 10;
        }
        else if (argv[i][0] != '-')
        {
            filepath = argv[i];
        }
    }

    FILE* fp = stdin;
    if (filepath && strcmp(filepath, "-") != 0)
    {
        fp = fopen(filepath, "r");
        if (!fp)
        {
            fprintf(stderr, "head: cannot open '%s'\n", filepath);
            return 1;
        }
    }

    char line[256];
    int count = 0;

    while (count < max_lines && fgets(line, sizeof(line), fp))
    {
        printf("%s", line);
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] != '\n')
            printf("\n");
        count++;
    }

    if (fp != stdin)
    {
        fclose(fp);
    }

    return 0;
}
