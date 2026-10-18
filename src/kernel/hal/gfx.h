#pragma once

#include <stdint.h>
#include <stdbool.h>

#define GFX_COLOR_BLACK             0x00000000
#define GFX_COLOR_WHITE             0x00FFFFFF
#define GFX_COLOR_RED               0x00EF4444
#define GFX_COLOR_GREEN             0x0022C55E
#define GFX_COLOR_BLUE              0x003B82F6
#define GFX_COLOR_DARK_BLUE         0x000F172A
#define GFX_COLOR_SLATE_BG          0x001E293B
#define GFX_COLOR_SLATE_PANEL       0x00334155
#define GFX_COLOR_LIGHT_SLATE       0x0064748B
#define GFX_COLOR_CYAN              0x0006B6D4
#define GFX_COLOR_EMERALD           0x0010B981
#define GFX_COLOR_AMBER             0x00F59E0B
#define GFX_COLOR_ROSE              0x00F43F5E
#define GFX_COLOR_TITLEBAR          0x001D4ED8
#define GFX_COLOR_TITLEBAR_INACTIVE 0x00475569
#define GFX_COLOR_TRANSPARENT       0xFF000000

typedef struct {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
} GFX_Rect;

bool GFX_Initialize(uint32_t width, uint32_t height);
void GFX_SwapBuffers(void);
uint32_t* GFX_GetBackbuffer(void);
uint32_t GFX_GetWidth(void);
uint32_t GFX_GetHeight(void);

void GFX_SetClip(int32_t x, int32_t y, int32_t width, int32_t height);
void GFX_ResetClip(void);

void GFX_Clear(uint32_t color);
void GFX_PutPixel(int32_t x, int32_t y, uint32_t color);
uint32_t GFX_GetPixel(int32_t x, int32_t y);
void GFX_DrawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color);
void GFX_DrawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
void GFX_FillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
void GFX_DrawCircle(int32_t xc, int32_t yc, int32_t r, uint32_t color);
void GFX_FillCircle(int32_t xc, int32_t yc, int32_t r, uint32_t color);

void GFX_DrawChar(int32_t x, int32_t y, char c, uint32_t fgColor, uint32_t bgColor);
void GFX_DrawString(int32_t x, int32_t y, const char* str, uint32_t fgColor, uint32_t bgColor);

void GFX_DrawCursor(int32_t x, int32_t y);
