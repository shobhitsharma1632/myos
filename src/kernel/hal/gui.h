#pragma once

#include <stdint.h>
#include <stdbool.h>

struct GUI_Window;
typedef void (*GUI_DrawCallback)(struct GUI_Window* win);

typedef struct GUI_Window {
    uint32_t id;
    const char* title;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    bool isVisible;
    bool isFocused;
    bool isDragging;
    int32_t dragOffsetX;
    int32_t dragOffsetY;
    uint32_t titleColor;
    GUI_DrawCallback onDraw;
    void* userData;
    struct GUI_Window* next;
} GUI_Window;

bool GUI_Initialize(uint32_t width, uint32_t height);
GUI_Window* GUI_CreateWindow(const char* title, int32_t x, int32_t y, int32_t width, int32_t height, GUI_DrawCallback onDraw);
void GUI_DestroyWindow(GUI_Window* win);
void GUI_FocusWindow(GUI_Window* win);
void GUI_Render(void);
void GUI_Task(void);
void GUI_Start(void);
bool GUI_IsRunning(void);
