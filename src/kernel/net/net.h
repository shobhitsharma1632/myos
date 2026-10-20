#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define MAKE_IPV4(a,b,c,d)  (((uint32_t)(d) << 24) | ((uint32_t)(c) << 16) | ((uint32_t)(b) << 8) | (uint32_t)(a))

static inline uint16_t htons(uint16_t v)
{
    return (uint16_t)((v << 8) | (v >> 8));
}

static inline uint16_t ntohs(uint16_t v)
{
    return htons(v);
}

static inline uint32_t htonl(uint32_t v)
{
    return ((v << 24) & 0xFF000000) |
           ((v << 8)  & 0x00FF0000) |
           ((v >> 8)  & 0x0000FF00) |
           ((v >> 24) & 0x000000FF);
}

static inline uint32_t ntohl(uint32_t v)
{
    return htonl(v);
}

typedef struct {
    char     name[8];
    uint8_t  mac[6];
    uint32_t ip;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns;
    bool     link_up;
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t rx_bytes;
    uint32_t tx_bytes;
} NetInterface;

void          Net_Initialize(void);
NetInterface* Net_GetInterface(void);
void          Net_Poll(void);
void          Net_ReceivePacket(const void* packet, size_t len);
uint16_t      net_checksum(const void* data, size_t len);
void          ipv4_to_str(uint32_t ip, char* buf);
uint32_t      str_to_ipv4(const char* str);
