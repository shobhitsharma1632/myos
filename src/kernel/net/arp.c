#include "arp.h"
#include "ethernet.h"
#include <arch/i686/e1000.h>
#include <hal/task.h>
#include <string.h>
#include "memory.h"
#include <debug.h>

#define MODULE "ARP"
#define ARP_CACHE_SIZE 16

typedef struct {
    uint32_t ip;
    uint8_t  mac[6];
    bool     valid;
} ARPCacheEntry;

static ARPCacheEntry g_ARPCache[ARP_CACHE_SIZE];
static uint32_t      g_ARPCacheNext = 0;

void ARP_Initialize(void)
{
    memset(g_ARPCache, 0, sizeof(g_ARPCache));
    g_ARPCacheNext = 0;
}

bool ARP_Lookup(uint32_t ip, uint8_t out_mac[6])
{
    for (int i = 0; i < ARP_CACHE_SIZE; i++)
    {
        if (g_ARPCache[i].valid && g_ARPCache[i].ip == ip)
        {
            if (out_mac)
            {
                memcpy(out_mac, g_ARPCache[i].mac, 6);
            }
            return true;
        }
    }
    return false;
}

static void arp_cache_add(uint32_t ip, const uint8_t mac[6])
{
    // Update existing entry if present
    for (int i = 0; i < ARP_CACHE_SIZE; i++)
    {
        if (g_ARPCache[i].valid && g_ARPCache[i].ip == ip)
        {
            memcpy(g_ARPCache[i].mac, mac, 6);
            return;
        }
    }

    // Insert into next available / round-robin slot
    uint32_t idx = g_ARPCacheNext;
    g_ARPCache[idx].ip = ip;
    memcpy(g_ARPCache[idx].mac, mac, 6);
    g_ARPCache[idx].valid = true;

    g_ARPCacheNext = (g_ARPCacheNext + 1) % ARP_CACHE_SIZE;

    char ipStr[16];
    ipv4_to_str(ip, ipStr);
    log_debug(MODULE, "Learned ARP mapping: %s -> %02x:%02x:%02x:%02x:%02x:%02x",
              ipStr, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void ARP_Request(NetInterface* netif, uint32_t target_ip)
{
    if (!netif) return;

    ARPHeader req;
    req.htype = htons(ARP_HTYPE_ETHERNET);
    req.ptype = htons(ARP_PTYPE_IPV4);
    req.hlen  = 6;
    req.plen  = 4;
    req.opcode = htons(ARP_OP_REQUEST);

    memcpy(req.sender_mac, netif->mac, 6);
    req.sender_ip = netif->ip;

    memset(req.target_mac, 0, 6);
    req.target_ip = target_ip;

    Ethernet_Send(netif, ETHERNET_BROADCAST_MAC, ETHERTYPE_ARP, &req, sizeof(ARPHeader));
}

bool ARP_Resolve(NetInterface* netif, uint32_t target_ip, uint8_t out_mac[6])
{
    if (!netif) return false;

    // Self IP loopback resolution
    if (target_ip == netif->ip)
    {
        if (out_mac) memcpy(out_mac, netif->mac, 6);
        return true;
    }

    // Broadcast IP
    if (target_ip == 0xFFFFFFFF)
    {
        if (out_mac) memcpy(out_mac, ETHERNET_BROADCAST_MAC, 6);
        return true;
    }

    // Check ARP cache
    if (ARP_Lookup(target_ip, out_mac))
        return true;

    // Send ARP Request and poll with Task_Yield()
    for (int retry = 0; retry < 3; retry++)
    {
        ARP_Request(netif, target_ip);

        for (int timeout = 0; timeout < 15; timeout++)
        {
            E1000_Poll();
            if (ARP_Lookup(target_ip, out_mac))
                return true;

            Task_Yield();
        }
    }

    return false;
}

void ARP_Handle(NetInterface* netif, const void* packet, size_t len)
{
    if (!netif || !packet || len < sizeof(ARPHeader))
        return;

    const ARPHeader* arp = (const ARPHeader*)packet;

    if (ntohs(arp->htype) != ARP_HTYPE_ETHERNET || ntohs(arp->ptype) != ARP_PTYPE_IPV4)
        return;

    uint16_t opcode = ntohs(arp->opcode);

    // Always record sender MAC in cache
    arp_cache_add(arp->sender_ip, arp->sender_mac);

    if (opcode == ARP_OP_REQUEST)
    {
        // Check if request is for our IP
        if (arp->target_ip == netif->ip)
        {
            ARPHeader reply;
            reply.htype = htons(ARP_HTYPE_ETHERNET);
            reply.ptype = htons(ARP_PTYPE_IPV4);
            reply.hlen  = 6;
            reply.plen  = 4;
            reply.opcode = htons(ARP_OP_REPLY);

            memcpy(reply.sender_mac, netif->mac, 6);
            reply.sender_ip = netif->ip;

            memcpy(reply.target_mac, arp->sender_mac, 6);
            reply.target_ip = arp->sender_ip;

            Ethernet_Send(netif, arp->sender_mac, ETHERTYPE_ARP, &reply, sizeof(ARPHeader));
        }
    }
}
