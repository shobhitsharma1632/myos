#include "../libuser.h"

int main(int argc, char* argv[])
{
    FILE* files[8];
    int num_files = 0;

    for (int i = 1; i < argc && num_files < 8; i++)
    {
        FILE* fp = fopen(argv[i], "w");
        if (fp)
        {
            files[num_files++] = fp;
        }
        else
        {
            fprintf(stderr, "tee: failed to open '%s'\n", argv[i]);
        }
    }

    char buffer[256];
    int n;

    while ((n = sys_read_file(0, buffer, sizeof(buffer) - 1)) > 0)
    {
        buffer[n] = '\0';
        sys_write_file(1, buffer, n);

        for (int i = 0; i < num_files; i++)
        {
            fwrite(buffer, 1, n, files[i]);
        }
    }

    for (int i = 0; i < num_files; i++)
    {
        fclose(files[i]);
    }

    return 0;
}
