#include "audio.h"
#include <arch/i686/sb16.h>
#include <hal/task.h>
#include <debug.h>
#include <stdio.h>
#include "memory.h"
#include <string.h>

#define MODULE "Audio"

static audio_status_t g_AudioStatus;

bool Audio_Initialize(void)
{
    memset(&g_AudioStatus, 0, sizeof(g_AudioStatus));
    g_AudioStatus.volume = 200;

    bool sb16_ok = SB16_Initialize(SB16_DEFAULT_BASE);
    g_AudioStatus.hardware_present = sb16_ok;

    if (sb16_ok)
    {
        SB16_SetVolume(g_AudioStatus.volume);
        log_info(MODULE, "Audio Subsystem Online: Sound Blaster 16 Active.");
    }
    else
    {
        log_warn(MODULE, "Audio Subsystem: Sound Blaster 16 not found (Software/PC Speaker Fallback).");
    }

    return true;
}

bool Audio_PlayPCM(const void* data, uint32_t length, uint16_t sample_rate, uint8_t bits, uint8_t channels, bool blocking)
{
    if (!data || length == 0) return false;

    if (g_AudioStatus.hardware_present)
    {
        g_AudioStatus.current_rate     = sample_rate;
        g_AudioStatus.current_bits     = bits;
        g_AudioStatus.current_channels = channels;
        g_AudioStatus.total_samples   += length;
        g_AudioStatus.is_playing       = true;

        SB16_Play(data, length, sample_rate, bits, channels);

        if (blocking)
        {
            while (SB16_IsPlaying())
            {
                Task_Yield();
            }
            g_AudioStatus.is_playing = false;
        }

        return true;
    }

    return false;
}

void Audio_Stop(void)
{
    if (g_AudioStatus.hardware_present)
    {
        SB16_Stop();
    }
    g_AudioStatus.is_playing = false;
}

bool Audio_IsPlaying(void)
{
    if (g_AudioStatus.hardware_present)
    {
        return SB16_IsPlaying();
    }
    return false;
}

void Audio_SetVolume(uint8_t volume)
{
    g_AudioStatus.volume = volume;
    if (g_AudioStatus.hardware_present)
    {
        SB16_SetVolume(volume);
    }
}

void Audio_GetStatus(audio_status_t* status)
{
    if (!status) return;
    if (g_AudioStatus.hardware_present)
    {
        g_AudioStatus.is_playing = SB16_IsPlaying();
    }
    memcpy(status, &g_AudioStatus, sizeof(audio_status_t));
}

void Audio_DumpStatus(void)
{
    audio_status_t st;
    Audio_GetStatus(&st);

    printf("\n=== Sound Blaster 16 & Audio Subsystem Status ===\n");
    printf("  Hardware Present : %s\n", st.hardware_present ? "YES (Sound Blaster 16)" : "NO");
    printf("  Playback State   : %s\n", st.is_playing ? "PLAYING" : "IDLE");
    printf("  Current Volume   : %u / 255\n", st.volume);
    printf("  Total Bytes Out  : %u bytes\n", st.total_samples);
    if (st.hardware_present)
    {
        const SB16_Device* dev = SB16_GetDevice();
        printf("  I/O Base Port    : 0x%03X (IRQ %u, DMA8 %u, DMA16 %u)\n",
               dev->base_port, dev->irq, dev->dma8, dev->dma16);
        printf("  DSP Firmware Ver : v%u.%u\n", dev->version_major, dev->version_minor);
    }
    printf("==================================================\n\n");
}
