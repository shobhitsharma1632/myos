#include "../libuser.h"

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        printf("Usage: curl <url> | curl <host> [port] [path]\n");
        printf("Example: curl http://10.0.2.15/readme.txt\n");
        return 1;
    }

    char host[64] = "10.0.2.15";
    uint16_t port = 80;
    char path[128] = "/";

    if (argc >= 4)
    {
        strncpy(host, argv[1], sizeof(host) - 1);
        port = (uint16_t)atoi(argv[2]);
        strncpy(path, argv[3], sizeof(path) - 1);
    }
    else if (argc >= 3 && argv[2][0] != '/')
    {
        strncpy(host, argv[1], sizeof(host) - 1);
        port = (uint16_t)atoi(argv[2]);
    }
    else
    {
        const char* url = argv[1];
        if (strncmp(url, "http://", 7) == 0)
        {
            url += 7;
        }

        // Find path component
        const char* slash = strchr(url, '/');
        if (slash)
        {
            strncpy(path, slash, sizeof(path) - 1);
            size_t host_len = slash - url;
            if (host_len >= sizeof(host)) host_len = sizeof(host) - 1;
            strncpy(host, url, host_len);
            host[host_len] = '\0';
        }
        else
        {
            strncpy(host, url, sizeof(host) - 1);
            strcpy(path, "/");
        }

        // Check for port in host
        char* colon = strchr(host, ':');
        if (colon)
        {
            *colon = '\0';
            port = (uint16_t)atoi(colon + 1);
        }
    }

    uint32_t ip = inet_addr(host);
    if (ip == 0)
    {
        // Default to local IP if "localhost"
        if (strcmp(host, "localhost") == 0)
        {
            ip = inet_addr("10.0.2.15");
        }
        else
        {
            fprintf(stderr, "curl: invalid host '%s'\n", host);
            return 1;
        }
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        fprintf(stderr, "curl: failed to create socket\n");
        return 1;
    }

    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(port);
    saddr.sin_addr.s_addr = ip;

    if (connect(sock, (struct sockaddr*)&saddr, sizeof(saddr)) < 0)
    {
        fprintf(stderr, "curl: could not connect to %s:%u\n", host, port);
        sys_close(sock);
        return 1;
    }

    char request[512];
    sprintf(request, "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: myOS-curl/1.0\r\nConnection: close\r\n\r\n", path, host);

    send(sock, request, strlen(request), 0);

    char buffer[256];
    int bytes_received;
    while ((bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0)
    {
        buffer[bytes_received] = '\0';
        printf("%s", buffer);
    }

    sys_close(sock);
    return 0;
}
