#include "../libuser.h"

int main(int argc, char* argv[])
{
    FILE* fp = stdin;
    const char* path = "";
    bool is_stdin = (argc < 2 || strcmp(argv[1], "-") == 0);

    if (!is_stdin)
    {
        path = argv[1];
        fp = fopen(path, "r");
        if (!fp)
        {
            fprintf(stderr, "wc: cannot open '%s'\n", path);
            return 1;
        }
    }

    int lines = 0;
    int words = 0;
    int bytes = 0;
    bool in_word = false;

    int c;
    while ((c = fgetc(fp)) != EOF)
    {
        bytes++;
        if (c == '\n')
            lines++;

        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
        {
            in_word = false;
        }
        else if (!in_word)
        {
            in_word = true;
            words++;
        }
    }

    if (!is_stdin)
    {
        fclose(fp);
        printf("  %d  %d  %d  %s\n", lines, words, bytes, path);
    }
    else
    {
        printf("  %d  %d  %d\n", lines, words, bytes);
    }

    return 0;
}
