#include "ipv4.h"
#include "ethernet.h"
#include "arp.h"
#include "icmp.h"
#include "udp.h"
#include "tcp.h"
#include <memory.h>
#include <string.h>
#include <debug.h>

#define MODULE "IPv4"

static uint16_t g_IPv4PacketID = 1;

bool IPv4_Send(NetInterface* netif, uint32_t dst_ip, uint8_t protocol, const void* payload, size_t len)
{
    if (!netif || !payload || len > 1480)
        return false;

    // Handle loopback directly to protocol handlers
    if (dst_ip == netif->ip || (dst_ip & 0xFF) == 127)
    {
        if (protocol == IPV4_PROTO_TCP)
        {
            TCP_Handle(netif, netif->ip, dst_ip, payload, len);
            return true;
        }
        else if (protocol == IPV4_PROTO_UDP)
        {
            UDP_Handle(netif, netif->ip, payload, len);
            return true;
        }
        else if (protocol == IPV4_PROTO_ICMP)
        {
            ICMP_Handle(netif, netif->ip, payload, len);
            return true;
        }
    }

    // Determine target next hop IP
    uint32_t next_hop = dst_ip;
    if (dst_ip != 0xFFFFFFFF && (dst_ip & netif->netmask) != (netif->ip & netif->netmask))
    {
        next_hop = netif->gateway;
    }

    // Resolve hardware MAC address
    uint8_t dst_mac[6];
    if (!ARP_Resolve(netif, next_hop, dst_mac))
    {
        char ipStr[16];
        ipv4_to_str(next_hop, ipStr);
        log_warn(MODULE, "Could not resolve MAC address for %s", ipStr);
        return false;
    }

    static uint8_t s_ipv4_tx_buffer[1500];
    IPv4Header* hdr = (IPv4Header*)s_ipv4_tx_buffer;

    hdr->version_ihl     = 0x45; // IPv4, IHL = 5 (20 bytes)
    hdr->tos             = 0;
    hdr->total_length    = htons(sizeof(IPv4Header) + len);
    hdr->id              = htons(g_IPv4PacketID++);
    hdr->flags_fragment  = 0;
    hdr->ttl             = 64;
    hdr->protocol        = protocol;
    hdr->checksum        = 0;
    hdr->src_ip          = netif->ip;
    hdr->dst_ip          = dst_ip;

    hdr->checksum = net_checksum(hdr, sizeof(IPv4Header));

    memcpy(s_ipv4_tx_buffer + sizeof(IPv4Header), payload, len);

    return Ethernet_Send(netif, dst_mac, ETHERTYPE_IPV4, s_ipv4_tx_buffer, sizeof(IPv4Header) + len);
}

void IPv4_Handle(NetInterface* netif, const void* packet, size_t len)
{
    if (!netif || !packet || len < sizeof(IPv4Header))
        return;

    const IPv4Header* hdr = (const IPv4Header*)packet;

    // Verify IPv4 version
    if ((hdr->version_ihl >> 4) != 4)
        return;

    size_t header_len = (hdr->version_ihl & 0x0F) * 4;
    if (header_len < sizeof(IPv4Header) || len < header_len)
        return;

    // Verify header checksum
    if (net_checksum(hdr, header_len) != 0)
    {
        log_warn(MODULE, "IPv4 checksum mismatch, discarding packet.");
        return;
    }

    // Check if packet is intended for us or broadcast
    if (hdr->dst_ip != netif->ip && hdr->dst_ip != 0xFFFFFFFF)
        return;

    size_t total_len = ntohs(hdr->total_length);
    if (total_len > len || total_len < header_len)
        return;

    const uint8_t* payload = (const uint8_t*)packet + header_len;
    size_t payload_len = total_len - header_len;

    if (hdr->protocol == IPV4_PROTO_ICMP)
    {
        ICMP_Handle(netif, hdr->src_ip, payload, payload_len);
    }
    else if (hdr->protocol == IPV4_PROTO_UDP)
    {
        UDP_Handle(netif, hdr->src_ip, payload, payload_len);
    }
    else if (hdr->protocol == IPV4_PROTO_TCP)
    {
        TCP_Handle(netif, hdr->src_ip, hdr->dst_ip, payload, payload_len);
    }
}
