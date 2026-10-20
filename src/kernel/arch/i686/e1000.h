#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define E1000_VENDOR_INTEL      0x8086
#define E1000_DEV_82540EM       0x100E
#define E1000_DEV_82545EM       0x100F
#define E1000_DEV_82543GC       0x1004
#define E1000_DEV_82541PI       0x107C

typedef struct {
    bool     present;
    uint8_t  bus;
    uint8_t  slot;
    uint8_t  func;
    uint8_t  irq;
    uint32_t mem_base;      // Virtual address of mapped MMIO space
    uint32_t mem_phys;      // Physical BAR0 base address
    uint8_t  mac[6];
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t rx_bytes;
    uint32_t tx_bytes;
} E1000Device;

bool           E1000_Initialize(void);
bool           E1000_IsPresent(void);
const uint8_t* E1000_GetMAC(void);
bool           E1000_SendPacket(const void* data, size_t len);
void           E1000_Poll(void);
void           E1000_GetStats(uint32_t* rx_pkts, uint32_t* tx_pkts, uint32_t* rx_bytes, uint32_t* tx_bytes);
