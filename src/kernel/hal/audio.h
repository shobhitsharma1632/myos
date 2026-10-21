#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifndef _AUDIO_STATUS_T_DEFINED
#define _AUDIO_STATUS_T_DEFINED
typedef struct {
    bool     hardware_present;
    bool     is_playing;
    uint8_t  volume;
    uint16_t current_rate;
    uint8_t  current_bits;
    uint8_t  current_channels;
    uint32_t total_samples;
} audio_status_t;
#endif

bool Audio_Initialize(void);
bool Audio_PlayPCM(const void* data, uint32_t length, uint16_t sample_rate, uint8_t bits, uint8_t channels, bool blocking);
void Audio_Stop(void);
bool Audio_IsPlaying(void);
void Audio_SetVolume(uint8_t volume);
void Audio_GetStatus(audio_status_t* status);
void Audio_DumpStatus(void);
