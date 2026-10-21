#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define USER_GUI_BUFFER_BASE 0x60000000

#ifndef _GUI_EVENT_T_DEFINED
#define _GUI_EVENT_T_DEFINED
// GUI Input Event Types
#define GUI_EVENT_NONE         0
#define GUI_EVENT_MOUSE_DOWN   1
#define GUI_EVENT_MOUSE_UP     2
#define GUI_EVENT_MOUSE_MOVE   3
#define GUI_EVENT_KEY_DOWN     4
#define GUI_EVENT_KEY_UP       5
#define GUI_EVENT_FOCUS        6
#define GUI_EVENT_BLUR         7
#define GUI_EVENT_CLOSE        8

typedef struct {
    uint32_t type;          // GUI_EVENT_*
    uint32_t window_id;     // Target window ID
    int32_t  mouse_x;       // Relative to client area
    int32_t  mouse_y;       // Relative to client area
    uint32_t button;        // 1 = Left, 2 = Right
    uint32_t key_char;      // ASCII character
    uint32_t key_code;      // Key scancode / code
    uint32_t modifiers;     // Shift, Ctrl, Alt
} gui_event_t;
#endif

#define GUI_MAX_EVENTS 32

typedef struct {
    gui_event_t events[GUI_MAX_EVENTS];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
} GUI_EventQueue;

struct GUI_Window;
typedef void (*GUI_DrawCallback)(struct GUI_Window* win);

typedef struct GUI_Window {
    uint32_t id;
    uint32_t owner_pid;         // 0 if kernel, else user PID
    char     title[64];
    int32_t  x;
    int32_t  y;
    int32_t  width;
    int32_t  height;
    bool     isVisible;
    bool     isFocused;
    bool     isDragging;
    int32_t  dragOffsetX;
    int32_t  dragOffsetY;
    uint32_t titleColor;
    GUI_DrawCallback onDraw;    // Kernel-internal callback (NULL for client windows)
    void*    userData;

    // Client-backed window fields
    void*    phys_buffer;       // Physical frame base address (for compositor blit)
    uint32_t user_virt_buffer;  // Virtual address mapped in client address space
    size_t   buffer_pages;      // Number of physical pages allocated
    int32_t  client_w;          // Client area width (width - 2)
    int32_t  client_h;          // Client area height (height - TITLEBAR_HEIGHT - 2)
    bool     is_damaged;        // Dirty flag

    GUI_EventQueue events;      // Input event FIFO

    struct GUI_Window* next;
} GUI_Window;

bool GUI_Initialize(uint32_t width, uint32_t height);
GUI_Window* GUI_CreateWindow(const char* title, int32_t x, int32_t y, int32_t width, int32_t height, GUI_DrawCallback onDraw);
GUI_Window* GUI_CreateClientWindow(uint32_t owner_pid, const char* title, int32_t x, int32_t y, int32_t width, int32_t height);
GUI_Window* GUI_FindWindowById(uint32_t id);
void GUI_DestroyWindow(GUI_Window* win);
void GUI_FocusWindow(GUI_Window* win);
bool GUI_EnqueueEvent(GUI_Window* win, const gui_event_t* ev);
bool GUI_DequeueEvent(GUI_Window* win, gui_event_t* ev);
void GUI_DamageWindow(GUI_Window* win, int32_t x, int32_t y, int32_t w, int32_t h);
void GUI_CleanupProcessWindows(uint32_t pid);
void GUI_Render(void);
void GUI_Task(void);
void GUI_Start(void);
bool GUI_IsRunning(void);
