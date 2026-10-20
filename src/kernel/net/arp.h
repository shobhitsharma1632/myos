#pragma once

#include "net.h"

#define ARP_HTYPE_ETHERNET 1
#define ARP_PTYPE_IPV4     0x0800
#define ARP_OP_REQUEST     1
#define ARP_OP_REPLY       2

typedef struct {
    uint16_t htype;
    uint16_t ptype;
    uint8_t  hlen;
    uint8_t  plen;
    uint16_t opcode;
    uint8_t  sender_mac[6];
    uint32_t sender_ip;
    uint8_t  target_mac[6];
    uint32_t target_ip;
} __attribute__((packed)) ARPHeader;

void ARP_Initialize(void);
void ARP_Handle(NetInterface* netif, const void* packet, size_t len);
bool ARP_Resolve(NetInterface* netif, uint32_t target_ip, uint8_t out_mac[6]);
void ARP_Request(NetInterface* netif, uint32_t target_ip);
bool ARP_Lookup(uint32_t ip, uint8_t out_mac[6]);
