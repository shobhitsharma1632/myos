#pragma once

#include <stdint.h>
#include <stdbool.h>

#define IOAPIC_DEFAULT_PHYS_BASE 0xFEC00000

#define IOAPIC_REG_ID           0x00
#define IOAPIC_REG_VER          0x01
#define IOAPIC_REG_ARB          0x02
#define IOAPIC_REG_REDTBL_BASE  0x10

bool     IOAPIC_Initialize(uint32_t ioapic_phys_base);
uint32_t IOAPIC_Read(uint8_t reg);
void     IOAPIC_Write(uint8_t reg, uint32_t val);
void     IOAPIC_SetEntry(uint8_t index, uint8_t vector, uint8_t dest_apic_id, bool masked);
void     IOAPIC_MaskAll(void);
