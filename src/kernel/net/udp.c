#include "udp.h"
#include "ipv4.h"
#include <memory.h>
#include <string.h>
#include <debug.h>

#define MODULE "UDP"
#define UDP_MAX_SOCKETS 8

typedef struct {
    uint16_t    port;
    UDPCallback callback;
    bool        active;
} UDPSocket;

static UDPSocket g_UDPSockets[UDP_MAX_SOCKETS];

bool UDP_Bind(uint16_t port, UDPCallback callback)
{
    for (int i = 0; i < UDP_MAX_SOCKETS; i++)
    {
        if (!g_UDPSockets[i].active)
        {
            g_UDPSockets[i].port     = port;
            g_UDPSockets[i].callback = callback;
            g_UDPSockets[i].active   = true;
            return true;
        }
    }
    return false;
}

bool UDP_Send(NetInterface* netif, uint32_t dst_ip, uint16_t src_port, uint16_t dst_port, const void* payload, size_t len)
{
    if (!netif || !payload || len > 1472)
        return false;

    static uint8_t s_udp_tx_buffer[1480];
    UDPHeader* hdr = (UDPHeader*)s_udp_tx_buffer;

    hdr->src_port = htons(src_port);
    hdr->dst_port = htons(dst_port);
    hdr->length   = htons(sizeof(UDPHeader) + len);
    hdr->checksum = 0; // Checksum is optional in IPv4 UDP

    memcpy(s_udp_tx_buffer + sizeof(UDPHeader), payload, len);

    return IPv4_Send(netif, dst_ip, IPV4_PROTO_UDP, s_udp_tx_buffer, sizeof(UDPHeader) + len);
}

void UDP_Handle(NetInterface* netif, uint32_t src_ip, const void* packet, size_t len)
{
    (void)netif;
    if (!packet || len < sizeof(UDPHeader))
        return;

    const UDPHeader* hdr = (const UDPHeader*)packet;
    uint16_t dst_port = ntohs(hdr->dst_port);
    uint16_t src_port = ntohs(hdr->src_port);
    uint16_t ulen = ntohs(hdr->length);

    if (ulen > len || ulen < sizeof(UDPHeader))
        return;

    const uint8_t* payload = (const uint8_t*)packet + sizeof(UDPHeader);
    size_t payload_len = ulen - sizeof(UDPHeader);

    for (int i = 0; i < UDP_MAX_SOCKETS; i++)
    {
        if (g_UDPSockets[i].active && g_UDPSockets[i].port == dst_port)
        {
            if (g_UDPSockets[i].callback)
            {
                g_UDPSockets[i].callback(src_ip, src_port, payload, payload_len);
            }
            return;
        }
    }
}
