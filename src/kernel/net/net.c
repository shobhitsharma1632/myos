#include "net.h"
#include "ethernet.h"
#include "arp.h"
#include <arch/i686/e1000.h>
#include <string.h>
#include "memory.h"
#include <stdio.h>
#include <debug.h>

#define MODULE "Net"

static NetInterface g_NetIf;

uint16_t net_checksum(const void* data, size_t len)
{
    const uint16_t* ptr = (const uint16_t*)data;
    uint32_t sum = 0;

    while (len > 1)
    {
        sum += *ptr++;
        len -= 2;
    }

    if (len > 0)
    {
        sum += *(const uint8_t*)ptr;
    }

    while (sum >> 16)
    {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return (uint16_t)(~sum);
}

void ipv4_to_str(uint32_t ip, char* buf)
{
    uint8_t b1 = ip & 0xFF;
    uint8_t b2 = (ip >> 8) & 0xFF;
    uint8_t b3 = (ip >> 16) & 0xFF;
    uint8_t b4 = (ip >> 24) & 0xFF;
    sprintf(buf, "%u.%u.%u.%u", b1, b2, b3, b4);
}

uint32_t str_to_ipv4(const char* str)
{
    if (!str) return 0;
    uint32_t bytes[4] = {0, 0, 0, 0};
    int b = 0;

    while (*str && b < 4)
    {
        if (*str >= '0' && *str <= '9')
        {
            bytes[b] = bytes[b] * 10 + (*str - '0');
        }
        else if (*str == '.')
        {
            b++;
        }
        str++;
    }

    return MAKE_IPV4(bytes[0], bytes[1], bytes[2], bytes[3]);
}

void Net_Initialize(void)
{
    memset(&g_NetIf, 0, sizeof(g_NetIf));
    strcpy(g_NetIf.name, "eth0");

    if (E1000_Initialize())
    {
        const uint8_t* mac = E1000_GetMAC();
        memcpy(g_NetIf.mac, mac, 6);

        // Standard QEMU user networking defaults
        g_NetIf.ip      = MAKE_IPV4(10, 0, 2, 15);
        g_NetIf.netmask = MAKE_IPV4(255, 255, 255, 0);
        g_NetIf.gateway = MAKE_IPV4(10, 0, 2, 2);
        g_NetIf.dns     = MAKE_IPV4(10, 0, 2, 3);
        g_NetIf.link_up = true;

        ARP_Initialize();

        char ipStr[16], gwStr[16];
        ipv4_to_str(g_NetIf.ip, ipStr);
        ipv4_to_str(g_NetIf.gateway, gwStr);

        log_info(MODULE, "Network interface '%s' online: IP=%s, Gateway=%s, MAC=%02x:%02x:%02x:%02x:%02x:%02x",
                 g_NetIf.name, ipStr, gwStr,
                 g_NetIf.mac[0], g_NetIf.mac[1], g_NetIf.mac[2],
                 g_NetIf.mac[3], g_NetIf.mac[4], g_NetIf.mac[5]);
    }
    else
    {
        log_warn(MODULE, "Network subsystem running without hardware interface.");
    }
}

NetInterface* Net_GetInterface(void)
{
    return &g_NetIf;
}

void Net_Poll(void)
{
    E1000_Poll();
}

void Net_ReceivePacket(const void* packet, size_t len)
{
    if (!packet || len < sizeof(EthernetHeader))
        return;

    g_NetIf.rx_packets++;
    g_NetIf.rx_bytes += len;

    Ethernet_Handle(&g_NetIf, packet, len);
}
