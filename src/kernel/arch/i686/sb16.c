#include "sb16.h"
#include "dma.h"
#include <arch/i686/io.h>
#include <arch/i686/irq.h>
#include <debug.h>
#include "memory.h"
#include <string.h>

#define MODULE "SB16"

// Sound Blaster 16 Ports relative to base
#define SB16_REG_MIXER_ADDR 0x04
#define SB16_REG_MIXER_DATA 0x05
#define SB16_REG_RESET      0x06
#define SB16_REG_READ       0x0A
#define SB16_REG_WRITE      0x0C
#define SB16_REG_STATUS     0x0C
#define SB16_REG_DATA_AVAIL 0x0E
#define SB16_REG_ACK_16BIT  0x0F

// DSP Commands
#define DSP_CMD_SET_TIME_CONST  0x40
#define DSP_CMD_SET_SAMPLE_RATE 0x41
#define DSP_CMD_SPEAKER_ON      0xD1
#define DSP_CMD_SPEAKER_OFF     0xD3
#define DSP_CMD_HALT_DMA8       0xD0
#define DSP_CMD_CONTINUE_DMA8   0xD4
#define DSP_CMD_HALT_DMA16      0xD5
#define DSP_CMD_CONTINUE_DMA16  0xD6
#define DSP_CMD_GET_VERSION     0xE1
#define DSP_CMD_OUTPUT_8BIT     0xC0
#define DSP_CMD_OUTPUT_16BIT    0xB0

// Mixer Registers
#define MIXER_REG_RESET         0x00
#define MIXER_REG_VOICE_VOL     0x04
#define MIXER_REG_MASTER_VOL    0x22
#define MIXER_REG_IRQ_SETUP     0x80
#define MIXER_REG_DMA_SETUP     0x81

static SB16_Device g_SB16;

static inline void sb16_delay(void)
{
    for (volatile int i = 0; i < 20; i++)
    {
        i686_iowait();
    }
}

static void sb16_write_dsp(uint8_t value)
{
    for (int timeout = 0; timeout < 10000; timeout++)
    {
        if (!(i686_inb(g_SB16.base_port + SB16_REG_STATUS) & 0x80))
        {
            i686_outb(g_SB16.base_port + SB16_REG_WRITE, value);
            return;
        }
        sb16_delay();
    }
}

static uint8_t sb16_read_dsp(void)
{
    for (int timeout = 0; timeout < 10000; timeout++)
    {
        if (i686_inb(g_SB16.base_port + SB16_REG_DATA_AVAIL) & 0x80)
        {
            return i686_inb(g_SB16.base_port + SB16_REG_READ);
        }
        sb16_delay();
    }
    return 0;
}

static void sb16_write_mixer(uint8_t reg, uint8_t val)
{
    i686_outb(g_SB16.base_port + SB16_REG_MIXER_ADDR, reg);
    i686_outb(g_SB16.base_port + SB16_REG_MIXER_DATA, val);
}

static uint8_t sb16_read_mixer(uint8_t reg)
{
    i686_outb(g_SB16.base_port + SB16_REG_MIXER_ADDR, reg);
    return i686_inb(g_SB16.base_port + SB16_REG_MIXER_DATA);
}

static bool sb16_reset_dsp(uint16_t port)
{
    // 1. Write 1 to reset port
    i686_outb(port + SB16_REG_RESET, 1);

    // 2. Wait at least 3 microseconds
    for (int i = 0; i < 50; i++) sb16_delay();

    // 3. Write 0 to reset port
    i686_outb(port + SB16_REG_RESET, 0);

    // 4. Poll Data Available port for ready byte 0xAA
    for (int timeout = 0; timeout < 1000; timeout++)
    {
        if (i686_inb(port + SB16_REG_DATA_AVAIL) & 0x80)
        {
            uint8_t status = i686_inb(port + SB16_REG_READ);
            if (status == 0xAA)
            {
                return true;
            }
        }
        sb16_delay();
    }

    return false;
}

static void SB16_IRQHandler(Registers* regs)
{
    (void)regs;

    // Acknowledge 8-bit and 16-bit interrupts on SB16 DSP
    i686_inb(g_SB16.base_port + SB16_REG_DATA_AVAIL);
    i686_inb(g_SB16.base_port + SB16_REG_ACK_16BIT);

    g_SB16.is_playing = false;
}

bool SB16_Initialize(uint16_t base_port)
{
    memset(&g_SB16, 0, sizeof(g_SB16));
    g_SB16.base_port = base_port ? base_port : SB16_DEFAULT_BASE;
    g_SB16.irq       = SB16_DEFAULT_IRQ;
    g_SB16.dma8      = SB16_DEFAULT_DMA8;
    g_SB16.dma16     = SB16_DEFAULT_DMA16;

    DMA_Initialize();

    // Probe base ports: 0x220, then 0x240 fallback
    if (!sb16_reset_dsp(g_SB16.base_port))
    {
        g_SB16.base_port = 0x240;
        if (!sb16_reset_dsp(g_SB16.base_port))
        {
            log_warn(MODULE, "Sound Blaster 16 hardware not detected on port 0x220 or 0x240.");
            return false;
        }
    }

    // Query DSP Version
    sb16_write_dsp(DSP_CMD_GET_VERSION);
    g_SB16.version_major = sb16_read_dsp();
    g_SB16.version_minor = sb16_read_dsp();

    // Check version (SB16 has DSP version 4.x)
    if (g_SB16.version_major < 4)
    {
        log_warn(MODULE, "Detected legacy Sound Blaster (DSP v%u.%u), expected SB16 (v4.x+)",
                 g_SB16.version_major, g_SB16.version_minor);
    }

    g_SB16.detected = true;

    // Configure Mixer for IRQ 5 (bit 1 of reg 0x80) and DMA 1 + DMA 5 (bits 1 & 5 of reg 0x81)
    sb16_write_mixer(MIXER_REG_IRQ_SETUP, 0x02);
    sb16_write_mixer(MIXER_REG_DMA_SETUP, 0x22);

    // Set Master and Voice Volume to default high level (0xEE)
    sb16_write_mixer(MIXER_REG_MASTER_VOL, 0xEE);
    sb16_write_mixer(MIXER_REG_VOICE_VOL, 0xEE);

    // Register IRQ 5 handler
    i686_IRQ_RegisterHandler(g_SB16.irq, SB16_IRQHandler);

    // Turn speaker on
    SB16_SpeakerOn();

    log_info(MODULE, "Sound Blaster 16 Online: Port 0x%x, IRQ %u, DMA8 %u, DMA16 %u, DSP v%u.%u",
             g_SB16.base_port, g_SB16.irq, g_SB16.dma8, g_SB16.dma16,
             g_SB16.version_major, g_SB16.version_minor);

    return true;
}

bool SB16_IsAvailable(void)
{
    return g_SB16.detected;
}

const SB16_Device* SB16_GetDevice(void)
{
    return &g_SB16;
}

void SB16_SetVolume(uint8_t volume)
{
    if (!g_SB16.detected) return;
    // SB16 master volume is 4-bit left and 4-bit right: (v >> 4) << 4 | (v >> 4)
    uint8_t v4 = volume >> 4;
    uint8_t val = (v4 << 4) | v4;
    sb16_write_mixer(MIXER_REG_MASTER_VOL, val);
    sb16_write_mixer(MIXER_REG_VOICE_VOL, val);
}

void SB16_SpeakerOn(void)
{
    if (!g_SB16.detected) return;
    sb16_write_dsp(DSP_CMD_SPEAKER_ON);
}

void SB16_SpeakerOff(void)
{
    if (!g_SB16.detected) return;
    sb16_write_dsp(DSP_CMD_SPEAKER_OFF);
}

void SB16_Stop(void)
{
    if (!g_SB16.detected) return;
    sb16_write_dsp(DSP_CMD_HALT_DMA8);
    sb16_write_dsp(DSP_CMD_HALT_DMA16);
    DMA_MaskChannel(g_SB16.dma8);
    DMA_MaskChannel(g_SB16.dma16);
    g_SB16.is_playing = false;
}

bool SB16_IsPlaying(void)
{
    return g_SB16.is_playing;
}

void SB16_Play(const void* pcm_data, uint32_t length, uint16_t sample_rate, uint8_t bits, uint8_t channels)
{
    if (!g_SB16.detected || !pcm_data || length == 0) return;

    if (length > DMA_BUFFER_SIZE)
    {
        length = DMA_BUFFER_SIZE;
    }

    // Copy audio data to low-memory ISA DMA buffer
    void* dma_buf = DMA_GetBuffer();
    if (!dma_buf) return;
    memcpy(dma_buf, pcm_data, length);

    g_SB16.is_playing = true;

    // Set DSP Sample Rate (Command 0x41)
    sb16_write_dsp(DSP_CMD_SET_SAMPLE_RATE);
    sb16_write_dsp((uint8_t)(sample_rate >> 8));
    sb16_write_dsp((uint8_t)(sample_rate & 0xFF));

    if (bits == 16)
    {
        // 16-bit DMA playback (Channel 5)
        DMA_SetupChannel(g_SB16.dma16, DMA_GetBufferPhys(), length, DMA_MODE_READ | DMA_MODE_SINGLE);

        uint32_t sample_count = length / (2 * (channels ? channels : 1));
        uint16_t count_val = (uint16_t)(sample_count - 1);

        uint8_t mode_byte = (channels == 2) ? 0x30 : 0x10; // Signed PCM, Stereo/Mono

        sb16_write_dsp(DSP_CMD_OUTPUT_16BIT);
        sb16_write_dsp(mode_byte);
        sb16_write_dsp((uint8_t)(count_val & 0xFF));
        sb16_write_dsp((uint8_t)(count_val >> 8));
    }
    else
    {
        // 8-bit DMA playback (Channel 1)
        DMA_SetupChannel(g_SB16.dma8, DMA_GetBufferPhys(), length, DMA_MODE_READ | DMA_MODE_SINGLE);

        uint32_t sample_count = length;
        uint16_t count_val = (uint16_t)(sample_count - 1);

        uint8_t mode_byte = (channels == 2) ? 0x20 : 0x00; // Unsigned PCM, Stereo/Mono

        sb16_write_dsp(DSP_CMD_OUTPUT_8BIT);
        sb16_write_dsp(mode_byte);
        sb16_write_dsp((uint8_t)(count_val & 0xFF));
        sb16_write_dsp((uint8_t)(count_val >> 8));
    }

    g_SB16.samples_played += length;
}
