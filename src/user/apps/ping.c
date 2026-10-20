#include "../libuser.h"

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        printf("Usage: %s <destination_ip>\n", argv[0]);
        printf("Example: %s 10.0.2.2\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char* target_str = argv[1];
    uint32_t target_ip = inet_addr(target_str);
    if (target_ip == 0)
    {
        fprintf(stderr, "ping: invalid IPv4 address '%s'\n", target_str);
        return EXIT_FAILURE;
    }

    printf("PING %s: 56 data bytes\n", target_str);

    int count = 4;
    int received = 0;

    for (int seq = 1; seq <= count; seq++)
    {
        int rtt = sys_net_ping(target_ip, 1000);
        if (rtt >= 0)
        {
            printf("64 bytes from %s: icmp_seq=%d time=%d ms\n", target_str, seq, rtt);
            received++;
        }
        else
        {
            printf("Request timeout for icmp_seq=%d\n", seq);
        }

        if (seq < count)
        {
            sys_sleep(10); // Sleep ~100ms between pings
        }
    }

    int loss = ((count - received) * 100) / count;
    printf("--- %s ping statistics ---\n", target_str);
    printf("%d packets transmitted, %d received, %d%% packet loss\n",
           count, received, loss);

    return (received > 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
