#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define MAX_CPUS 16

// ACPI Root System Description Pointer (RSDP) Descriptor (ACPI 1.0 / 2.0)
typedef struct {
    char     signature[8];   // "RSD PTR "
    uint8_t  checksum;       // First 20 bytes sum to 0
    char     oem_id[6];
    uint8_t  revision;       // 0 = ACPI 1.0, 2 = ACPI 2.0+
    uint32_t rsdt_address;   // Physical address of RSDT
} __attribute__((packed)) RSDPDescriptor;

// Standard ACPI System Description Table (SDT) Header
typedef struct {
    char     signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oem_id[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed)) ACPISDTHeader;

// Root System Description Table (RSDT)
typedef struct {
    ACPISDTHeader header;
    uint32_t      tables[];  // Array of pointers to SDTs
} __attribute__((packed)) RSDT;

// Multiple APIC Description Table (MADT)
typedef struct {
    ACPISDTHeader header;
    uint32_t      local_apic_address; // Physical address of Local APIC (typically 0xFEE00000)
    uint32_t      flags;              // Bit 0 = PC/AT dual-8259 PIC installed
    uint8_t       entries[];          // Variable length records
} __attribute__((packed)) MADT;

// MADT Record Header
typedef struct {
    uint8_t type;
    uint8_t length;
} __attribute__((packed)) MADTRecordHeader;

// MADT Entry Type 0: Processor Local APIC
typedef struct {
    MADTRecordHeader header;
    uint8_t          acpi_processor_id;
    uint8_t          apic_id;
    uint32_t         flags;          // Bit 0 = Enabled
} __attribute__((packed)) MADTRecordLocalAPIC;

// MADT Entry Type 1: I/O APIC
typedef struct {
    MADTRecordHeader header;
    uint8_t          ioapic_id;
    uint8_t          reserved;
    uint32_t         ioapic_address; // Physical address of I/O APIC (typically 0xFEC00000)
    uint32_t         gsi_base;       // Global System Interrupt Base
} __attribute__((packed)) MADTRecordIOAPIC;

// MADT Entry Type 2: Interrupt Source Override
typedef struct {
    MADTRecordHeader header;
    uint8_t          bus_source;     // 0 = ISA
    uint8_t          irq_source;     // Legacy IRQ source
    uint32_t         gsi;            // Global System Interrupt
    uint16_t         flags;          // Polarity and trigger flags
} __attribute__((packed)) MADTRecordIntOverride;

typedef struct {
    uint8_t  acpi_id;
    uint8_t  apic_id;
    bool     enabled;
    bool     is_bsp;
    bool     online;
    uint32_t ticks;
} CPU_Core;

typedef struct {
    bool     detected;
    uint32_t lapic_phys_base;
    uint32_t ioapic_phys_base;
    uint8_t  ioapic_id;
    uint32_t cpu_count;
    CPU_Core cpus[MAX_CPUS];
    uint8_t  irq_override[16];       // Maps ISA IRQ 0..15 to GSI
} ACPI_Info;

bool ACPI_Initialize(void);
const ACPI_Info* ACPI_GetInfo(void);
uint32_t ACPI_GetCPUCount(void);
bool ACPI_IsDualPICInstalled(void);
