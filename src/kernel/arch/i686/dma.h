#pragma once

#include <stdint.h>
#include <stdbool.h>

// ISA DMA Modes (Bits 2..3: Transfer Type, Bits 4..5: Mode)
#define DMA_MODE_VERIFY     0x00
#define DMA_MODE_WRITE      0x04  // From device to memory (ADC/Recording)
#define DMA_MODE_READ       0x08  // From memory to device (DAC/Playback)
#define DMA_MODE_AUTOINIT   0x10  // Auto-initialize mode
#define DMA_MODE_SINGLE     0x40  // Single cycle mode
#define DMA_MODE_BLOCK      0x80  // Block transfer mode

// Channel definitions
#define DMA_CHANNEL_SB16_8BIT  1  // Channel 1 for 8-bit audio
#define DMA_CHANNEL_SB16_16BIT 5  // Channel 5 for 16-bit audio

#define DMA_BUFFER_SIZE     16384 // 16 KiB ISA DMA buffer

void     DMA_Initialize(void);
bool     DMA_SetupChannel(uint8_t channel, uint32_t phys_addr, uint32_t length, uint8_t mode);
void     DMA_MaskChannel(uint8_t channel);
void     DMA_UnmaskChannel(uint8_t channel);
void*    DMA_GetBuffer(void);
uint32_t DMA_GetBufferPhys(void);
