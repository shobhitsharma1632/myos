#pragma once

#include "net.h"

#define IPV4_PROTO_ICMP 1
#define IPV4_PROTO_TCP  6
#define IPV4_PROTO_UDP  17

typedef struct {
    uint8_t  version_ihl;
    uint8_t  tos;
    uint16_t total_length;
    uint16_t id;
    uint16_t flags_fragment;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint32_t src_ip;
    uint32_t dst_ip;
} __attribute__((packed)) IPv4Header;

bool IPv4_Send(NetInterface* netif, uint32_t dst_ip, uint8_t protocol, const void* payload, size_t len);
void IPv4_Handle(NetInterface* netif, const void* packet, size_t len);
