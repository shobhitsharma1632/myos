#include "../libuser.h"

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        printf("Usage: wc <filepath>\n");
        return 1;
    }

    const char* path = argv[1];
    FILE* fp = fopen(path, "r");
    if (!fp)
    {
        printf("wc: cannot open '%s'\n", path);
        return 1;
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

    fclose(fp);

    printf("  %d  %d  %d  %s\n", lines, words, bytes, path);
    return 0;
}
