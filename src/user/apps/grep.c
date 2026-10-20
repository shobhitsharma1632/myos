#include "../libuser.h"

int main(int argc, char* argv[])
{
    if (argc < 3)
    {
        printf("Usage: grep <pattern> <filepath>\n");
        return 1;
    }

    const char* pattern = argv[1];
    const char* path = argv[2];

    FILE* fp = fopen(path, "r");
    if (!fp)
    {
        printf("grep: cannot open '%s'\n", path);
        return 1;
    }

    char line[256];
    int line_num = 1;
    int matches = 0;

    while (fgets(line, sizeof(line), fp))
    {
        if (strstr(line, pattern))
        {
            // Print matching line
            printf("[%s:%d] %s", path, line_num, line);
            // Ensure trailing newline if line was stripped
            size_t len = strlen(line);
            if (len > 0 && line[len - 1] != '\n')
                printf("\n");
            matches++;
        }
        line_num++;
    }

    fclose(fp);

    if (matches == 0)
    {
        return 1; // grep returns 1 when no pattern match found
    }
    return 0;
}
