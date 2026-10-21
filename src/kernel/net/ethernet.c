#include "ethernet.h"
#include "arp.h"
#include "ipv4.h"
#include <arch/i686/e1000.h>
#include <memory.h>
#include <string.h>

const uint8_t ETHERNET_BROADCAST_MAC[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

bool Ethernet_Send(NetInterface* netif, const uint8_t dst_mac[6], uint16_t ethertype, const void* payload, size_t len)
{
    if (!netif || !dst_mac || !payload || len > 1500)
        return false;

    static uint8_t s_eth_tx_buffer[1518];
    EthernetHeader* hdr = (EthernetHeader*)s_eth_tx_buffer;

    memcpy(hdr->dst_mac, dst_mac, 6);
    memcpy(hdr->src_mac, netif->mac, 6);
    hdr->ethertype = htons(ethertype);

    memcpy(s_eth_tx_buffer + sizeof(EthernetHeader), payload, len);

    size_t total_len = sizeof(EthernetHeader) + len;
    // Ethernet minimum frame length without FCS is 60 bytes
    if (total_len < 60)
    {
        memset(s_eth_tx_buffer + total_len, 0, 60 - total_len);
        total_len = 60;
    }

    if (E1000_SendPacket(s_eth_tx_buffer, total_len))
    {
        netif->tx_packets++;
        netif->tx_bytes += total_len;
        return true;
    }
    return false;
}

void Ethernet_Handle(NetInterface* netif, const void* frame, size_t len)
{
    if (!netif || !frame || len < sizeof(EthernetHeader))
        return;

    const EthernetHeader* hdr = (const EthernetHeader*)frame;

    // Check if frame is for us (unicast to our MAC, or broadcast)
    bool is_unicast = (memcmp(hdr->dst_mac, netif->mac, 6) == 0);
    bool is_bcast   = (memcmp(hdr->dst_mac, ETHERNET_BROADCAST_MAC, 6) == 0);

    if (!is_unicast && !is_bcast)
        return;

    uint16_t ethertype = ntohs(hdr->ethertype);
    const uint8_t* payload = (const uint8_t*)frame + sizeof(EthernetHeader);
    size_t payload_len = len - sizeof(EthernetHeader);

    if (ethertype == ETHERTYPE_ARP)
    {
        ARP_Handle(netif, payload, payload_len);
    }
    else if (ethertype == ETHERTYPE_IPV4)
    {
        IPv4_Handle(netif, payload, payload_len);
    }
}
