#pragma once

#include "net.h"

#define ICMP_TYPE_ECHO_REPLY   0
#define ICMP_TYPE_ECHO_REQUEST 8

typedef struct {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t sequence;
} __attribute__((packed)) ICMPHeader;

void    ICMP_Handle(NetInterface* netif, uint32_t src_ip, const void* packet, size_t len);
int32_t ICMP_Ping(NetInterface* netif, uint32_t target_ip, uint32_t timeout_ms);
