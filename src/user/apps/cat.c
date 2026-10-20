#include "../libuser.h"

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        printf("Usage: cat <filepath>\n");
        return 1;
    }

    const char* filepath = argv[1];
    printf("\n[User Cat] Opening file '%s' from Ring 3...\n", filepath);

    int fd = sys_open(filepath, O_RDONLY);
    if (fd < 0)
    {
        printf("[User Cat] Error: Cannot open '%s'!\n\n", filepath);
        return -1;
    }

    char buffer[128];
    int totalBytes = 0;
    int n;

    while ((n = sys_read_file(fd, buffer, sizeof(buffer) - 1)) > 0)
    {
        buffer[n] = '\0';
        sys_print(buffer);
        totalBytes += n;
    }

    sys_close(fd);
    printf("\n[User Cat] Successfully read %d bytes from '%s'.\n\n", totalBytes, filepath);
    return 0;
}
