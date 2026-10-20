#include "../libuser.h"

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    NetInfoUser info;
    memset(&info, 0, sizeof(info));

    if (sys_net_info(&info) < 0)
    {
        fprintf(stderr, "ifconfig: failed to query network interface.\n");
        return EXIT_FAILURE;
    }

    char ip_str[16];
    char mask_str[16];
    char gw_str[16];
    char dns_str[16];

    inet_ntoa(info.ip, ip_str);
    inet_ntoa(info.netmask, mask_str);
    inet_ntoa(info.gateway, gw_str);
    inet_ntoa(info.dns, dns_str);

    printf("eth0: flags=UP,BROADCAST,MULTICAST\n");
    printf("      ether %02x:%02x:%02x:%02x:%02x:%02x\n",
           info.mac[0], info.mac[1], info.mac[2],
           info.mac[3], info.mac[4], info.mac[5]);
    printf("      inet %s  netmask %s  gateway %s\n", ip_str, mask_str, gw_str);
    printf("      dns %s\n", dns_str);
    printf("      RX packets: %u  bytes: %u\n", info.rx_packets, info.rx_bytes);
    printf("      TX packets: %u  bytes: %u\n", info.tx_packets, info.tx_bytes);

    return EXIT_SUCCESS;
}
