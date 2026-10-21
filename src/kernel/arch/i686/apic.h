#pragma once

#include <stdint.h>
#include <stdbool.h>

#define LAPIC_DEFAULT_PHYS_BASE 0xFEE00000

// LAPIC Register Offsets
#define LAPIC_REG_ID            0x0020  // Local APIC ID
#define LAPIC_REG_VERSION       0x0030  // Local APIC Version
#define LAPIC_REG_TPR           0x0080  // Task Priority Register
#define LAPIC_REG_APR           0x0090  // Arbitration Priority Register
#define LAPIC_REG_PPR           0x00A0  // Processor Priority Register
#define LAPIC_REG_EOI           0x00B0  // End of Interrupt
#define LAPIC_REG_RRD           0x00C0  // Remote Read Register
#define LAPIC_REG_LDR           0x00D0  // Logical Destination Register
#define LAPIC_REG_DFR           0x00E0  // Destination Format Register
#define LAPIC_REG_SVR           0x00F0  // Spurious Interrupt Vector Register
#define LAPIC_REG_ISR           0x0100  // In-Service Register (0x100..0x170)
#define LAPIC_REG_TMR           0x0180  // Trigger Mode Register (0x180..0x1F0)
#define LAPIC_REG_IRR           0x0200  // Interrupt Request Register (0x200..0x270)
#define LAPIC_REG_ESR           0x0280  // Error Status Register
#define LAPIC_REG_ICR_LOW       0x0300  // Interrupt Command Register Low
#define LAPIC_REG_ICR_HIGH      0x0310  // Interrupt Command Register High
#define LAPIC_REG_LVT_TIMER     0x0320  // LVT Timer Register
#define LAPIC_REG_LVT_THERMAL   0x0330  // LVT Thermal Sensor Register
#define LAPIC_REG_LVT_PERF      0x0340  // LVT Performance Counter Register
#define LAPIC_REG_LVT_LINT0     0x0350  // LVT LINT0 Register
#define LAPIC_REG_LVT_LINT1     0x0360  // LVT LINT1 Register
#define LAPIC_REG_LVT_ERROR     0x0370  // LVT Error Register
#define LAPIC_REG_TIMER_INIT    0x0380  // Initial Count Register (for Timer)
#define LAPIC_REG_TIMER_CURR    0x0390  // Current Count Register (for Timer)
#define LAPIC_REG_TIMER_DIV     0x03E0  // Divide Configuration Register (for Timer)

// ICR Delivery Modes
#define LAPIC_ICR_FIXED         0x00000000
#define LAPIC_ICR_LOWPRI        0x00000100
#define LAPIC_ICR_SMI           0x00000200
#define LAPIC_ICR_NMI           0x00000400
#define LAPIC_ICR_INIT          0x00000500
#define LAPIC_ICR_STARTUP       0x00000600

// ICR Flags
#define LAPIC_ICR_PHYSICAL      0x00000000
#define LAPIC_ICR_LOGICAL       0x00000800
#define LAPIC_ICR_IDLE          0x00000000
#define LAPIC_ICR_BUSY          0x00001000
#define LAPIC_ICR_DEASSERT      0x00000000
#define LAPIC_ICR_ASSERT        0x00004000
#define LAPIC_ICR_EDGE          0x00000000
#define LAPIC_ICR_LEVEL         0x00008000

// SMP Inter-Processor Interrupt Vectors
#define SMP_IPI_RESCHEDULE_VECTOR       0xFA // Vector 250
#define SMP_IPI_TLB_SHOOTDOWN_VECTOR     0xFB // Vector 251

bool     LAPIC_Initialize(uint32_t lapic_phys_base);
void     LAPIC_Enable(void);
uint32_t LAPIC_GetId(void);
uint32_t LAPIC_Read(uint32_t reg);
void     LAPIC_Write(uint32_t reg, uint32_t val);
void     LAPIC_EOI(void);
void     LAPIC_SendIPI(uint8_t target_apic_id, uint32_t icr_low);
void     LAPIC_SendIPIBroadcast(uint8_t vector);
void     LAPIC_SendInit(uint8_t target_apic_id);
void     LAPIC_SendStartup(uint8_t target_apic_id, uint8_t page_vector);
