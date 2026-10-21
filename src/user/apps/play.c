#include <libuser.h>

static void generate_synth_tone(uint8_t* buffer, uint32_t length, uint16_t sample_rate, uint16_t freq)
{
    uint32_t period = sample_rate / freq;
    if (period == 0) period = 1;

    for (uint32_t i = 0; i < length; i++)
    {
        // Generate square wave (128 +/- 40)
        uint32_t phase = i % period;
        buffer[i] = (phase < period / 2) ? 168 : 88;
    }
}

int main(int argc, char* argv[])
{
    printf("==================================================\n");
    printf("  myOS Sound Blaster 16 Digital Audio Player\n");
    printf("==================================================\n");

    audio_status_t status;
    if (sys_audio_status(&status) == 0)
    {
        printf("  Audio Hardware: %s\n", status.hardware_present ? "Sound Blaster 16 Online" : "Software Synthesizer Fallback");
        printf("  Master Volume : %u / 255\n", status.volume);
    }

    if (argc >= 2 && strcmp(argv[1], "--synth") == 0)
    {
        uint16_t freq = 440; // Default 440 Hz (A4)
        if (argc >= 3)
        {
            freq = (uint16_t)atoi(argv[2]);
            if (freq < 50) freq = 50;
            if (freq > 8000) freq = 8000;
        }

        uint16_t sample_rate = 11025;
        uint32_t length = sample_rate; // 1 second
        uint8_t* buf = (uint8_t*)malloc(length);
        if (!buf)
        {
            printf("Error: Failed to allocate synthesis buffer!\n");
            return 1;
        }

        printf("  Synthesizing %u Hz Tone (11025 Hz 8-bit Mono, 1.0s)...\n", freq);
        generate_synth_tone(buf, length, sample_rate, freq);

        printf("  Streaming audio to Sound Blaster 16 DSP...\n");
        int res = sys_audio_play(buf, length, sample_rate, 8, 1, true);
        free(buf);

        if (res == 0)
        {
            printf("  Playback completed successfully!\n");
        }
        else
        {
            printf("  Note: Playback queued (result: %d)\n", res);
        }
        printf("==================================================\n");
        return 0;
    }

    const char* path = (argc >= 2) ? argv[1] : "/sounds/coin.wav";
    printf("  Loading audio file: '%s'...\n", path);

    int res = audio_play_wav(path, true);
    if (res == 0)
    {
        printf("  Playback of '%s' completed successfully!\n", path);
    }
    else
    {
        printf("  File playback failed (code %d). Generating synthetic chime fallback...\n", res);
        uint16_t sample_rate = 11025;
        uint32_t length = 4000;
        uint8_t* buf = (uint8_t*)malloc(length);
        if (buf)
        {
            generate_synth_tone(buf, length, sample_rate, 880); // 880 Hz beep
            sys_audio_play(buf, length, sample_rate, 8, 1, true);
            free(buf);
            printf("  Synthetic chime completed!\n");
        }
    }

    printf("==================================================\n");
    return 0;
}
