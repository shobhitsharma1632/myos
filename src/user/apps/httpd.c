#include "../libuser.h"

int main(int argc, char* argv[])
{
    uint16_t port = 80;
    bool once = false;
    int max_requests = -1;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--once") == 0 || strcmp(argv[i], "-t") == 0)
        {
            once = true;
            max_requests = 1;
        }
        else if (argv[i][0] >= '0' && argv[i][0] <= '9')
        {
            port = (uint16_t)atoi(argv[i]);
        }
    }

    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0)
    {
        fprintf(stderr, "httpd: failed to create socket\n");
        return 1;
    }

    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(port);
    saddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_sock, (struct sockaddr*)&saddr, sizeof(saddr)) < 0)
    {
        fprintf(stderr, "httpd: failed to bind to port %u\n", port);
        sys_close(server_sock);
        return 1;
    }

    if (listen(server_sock, 5) < 0)
    {
        fprintf(stderr, "httpd: failed to listen\n");
        sys_close(server_sock);
        return 1;
    }

    printf("myOS httpd: listening on port %u (Ring 3 User Mode)...\n", port);

    int requests_served = 0;
    while (max_requests < 0 || requests_served < max_requests)
    {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_len);
        if (client_sock < 0)
        {
            break;
        }

        char request_buf[512];
        int bytes = recv(client_sock, request_buf, sizeof(request_buf) - 1, 0);
        if (bytes > 0)
        {
            request_buf[bytes] = '\0';

            // Parse request line: "GET <path> HTTP/1.x"
            char method[16] = {0};
            char path[128] = {0};
            char* p = request_buf;

            int m = 0;
            while (*p && *p != ' ' && m < (int)sizeof(method) - 1) method[m++] = *p++;
            while (*p == ' ') p++;

            int u = 0;
            while (*p && *p != ' ' && *p != '\r' && *p != '\n' && u < (int)sizeof(path) - 1) path[u++] = *p++;

            printf("httpd: %s %s\n", method, path);

            if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0)
            {
                const char* body = "<html><head><title>myOS</title></head><body><h1>Welcome to myOS Web Server!</h1><p>Served by /bin/httpd.elf over Ring 3 TCP/IP stack.</p></body></html>\r\n";
                char header[256];
                sprintf(header, "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nContent-Length: %u\r\nConnection: close\r\n\r\n", strlen(body));
                send(client_sock, header, strlen(header), 0);
                send(client_sock, body, strlen(body), 0);
            }
            else
            {
                // Try serving file from disk
                int fd = sys_open(path, O_RDONLY);
                if (fd >= 0)
                {
                    char file_buf[1024];
                    int n = sys_read_file(fd, file_buf, sizeof(file_buf));
                    sys_close(fd);

                    char header[256];
                    sprintf(header, "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", n > 0 ? n : 0);
                    send(client_sock, header, strlen(header), 0);
                    if (n > 0)
                    {
                        send(client_sock, file_buf, n, 0);
                    }
                }
                else
                {
                    const char* not_found = "HTTP/1.0 404 Not Found\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n404 Not Found: Resource unavailable.\r\n";
                    send(client_sock, not_found, strlen(not_found), 0);
                }
            }
        }

        sys_close(client_sock);
        requests_served++;

        if (once) break;
    }

    printf("myOS httpd: shutting down after %d requests.\n", requests_served);
    sys_close(server_sock);
    return 0;
}
