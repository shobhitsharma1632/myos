#pragma once

#include <stdint.h>
#include <stdbool.h>

#define BGA_INDEX_PORT              0x01CE
#define BGA_DATA_PORT               0x01CF

#define VBE_DISPI_INDEX_ID          0x0
#define VBE_DISPI_INDEX_XRES        0x1
#define VBE_DISPI_INDEX_YRES        0x2
#define VBE_DISPI_INDEX_BPP         0x3
#define VBE_DISPI_INDEX_ENABLE      0x4
#define VBE_DISPI_INDEX_BANK        0x5
#define VBE_DISPI_INDEX_VIRT_WIDTH  0x6
#define VBE_DISPI_INDEX_VIRT_HEIGHT 0x7
#define VBE_DISPI_INDEX_X_OFFSET    0x8
#define VBE_DISPI_INDEX_Y_OFFSET    0x9

#define VBE_DISPI_DISABLED          0x00
#define VBE_DISPI_ENABLED           0x01
#define VBE_DISPI_LFB_ENABLED       0x40
#define VBE_DISPI_NOCLEARMEM        0x80

#define BGA_DEFAULT_LFB_PHYS        0xE0000000

bool BGA_IsAvailable(void);
uint16_t BGA_GetVersion(void);
uint32_t BGA_GetPhysicalFramebuffer(void);
bool BGA_SetVideoMode(uint32_t width, uint32_t height, uint32_t bpp);
void* BGA_GetFramebuffer(void);
uint32_t BGA_GetWidth(void);
uint32_t BGA_GetHeight(void);
uint32_t BGA_GetBpp(void);
uint32_t BGA_GetPitch(void);
