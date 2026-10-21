#pragma once

#include <stdint.h>
#include <stdbool.h>

#define SB16_DEFAULT_BASE   0x220
#define SB16_DEFAULT_IRQ    5
#define SB16_DEFAULT_DMA8   1
#define SB16_DEFAULT_DMA16  5

typedef struct {
    bool     detected;
    uint16_t base_port;
    uint8_t  irq;
    uint8_t  dma8;
    uint8_t  dma16;
    uint8_t  version_major;
    uint8_t  version_minor;
    bool     is_playing;
    uint32_t samples_played;
} SB16_Device;

bool        SB16_Initialize(uint16_t base_port);
bool        SB16_IsAvailable(void);
const SB16_Device* SB16_GetDevice(void);
void        SB16_SetVolume(uint8_t volume); // 0..255
void        SB16_Play(const void* pcm_data, uint32_t length, uint16_t sample_rate, uint8_t bits, uint8_t channels);
void        SB16_Stop(void);
bool        SB16_IsPlaying(void);
void        SB16_SpeakerOn(void);
void        SB16_SpeakerOff(void);
