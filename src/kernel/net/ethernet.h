#pragma once

#include "net.h"

#define ETHERTYPE_IPV4 0x0800
#define ETHERTYPE_ARP  0x0806

typedef struct {
    uint8_t  dst_mac[6];
    uint8_t  src_mac[6];
    uint16_t ethertype;
} __attribute__((packed)) EthernetHeader;

extern const uint8_t ETHERNET_BROADCAST_MAC[6];

bool Ethernet_Send(NetInterface* netif, const uint8_t dst_mac[6], uint16_t ethertype, const void* payload, size_t len);
void Ethernet_Handle(NetInterface* netif, const void* frame, size_t len);
