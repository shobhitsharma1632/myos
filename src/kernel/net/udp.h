#pragma once

#include "net.h"

typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed)) UDPHeader;

typedef void (*UDPCallback)(uint32_t src_ip, uint16_t src_port, const void* data, size_t len);

bool UDP_Send(NetInterface* netif, uint32_t dst_ip, uint16_t src_port, uint16_t dst_port, const void* payload, size_t len);
void UDP_Handle(NetInterface* netif, uint32_t src_ip, const void* packet, size_t len);
bool UDP_Bind(uint16_t port, UDPCallback callback);
