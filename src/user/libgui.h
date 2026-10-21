#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "libuser.h"

// Standard GUI Colors
#define GUI_COLOR_BLACK             0x00000000
#define GUI_COLOR_WHITE             0x00FFFFFF
#define GUI_COLOR_RED               0x00EF4444
#define GUI_COLOR_GREEN             0x0022C55E
#define GUI_COLOR_BLUE              0x003B82F6
#define GUI_COLOR_DARK_BLUE         0x000F172A
#define GUI_COLOR_SLATE_BG          0x001E293B
#define GUI_COLOR_SLATE_PANEL       0x00334155
#define GUI_COLOR_LIGHT_SLATE       0x0064748B
#define GUI_COLOR_CYAN              0x0006B6D4
#define GUI_COLOR_EMERALD           0x0010B981
#define GUI_COLOR_AMBER             0x00F59E0B
#define GUI_COLOR_ROSE              0x00F43F5E
#define GUI_COLOR_YELLOW            0x00EAB308
#define GUI_COLOR_MAGENTA           0x00D946EF
#define GUI_COLOR_TRANSPARENT       0xFF000000

typedef struct {
    int       id;
    int       width;
    int       height;
    int       client_w;
    int       client_h;
    uint32_t* buffer;
    bool      closed;
} gui_window_t;

// Window Lifecycle
gui_window_t* gui_create_window(const char* title, int x, int y, int width, int height);
void          gui_destroy_window(gui_window_t* win);
bool          gui_poll_event(gui_window_t* win, gui_event_t* ev);
void          gui_present(gui_window_t* win);
void          gui_damage(gui_window_t* win, int x, int y, int w, int h);

// Drawing Primitives (Zero-copy in User Space)
void gui_put_pixel(gui_window_t* win, int x, int y, uint32_t color);
uint32_t gui_get_pixel(gui_window_t* win, int x, int y);
void gui_clear(gui_window_t* win, uint32_t color);
void gui_draw_line(gui_window_t* win, int x0, int y0, int x1, int y1, uint32_t color);
void gui_draw_rect(gui_window_t* win, int x, int y, int w, int h, uint32_t color);
void gui_fill_rect(gui_window_t* win, int x, int y, int w, int h, uint32_t color);
void gui_draw_circle(gui_window_t* win, int xc, int yc, int r, uint32_t color);
void gui_fill_circle(gui_window_t* win, int xc, int yc, int r, uint32_t color);

// Text & Font Rendering
void gui_draw_char(gui_window_t* win, int x, int y, char c, uint32_t fg, uint32_t bg);
void gui_draw_text(gui_window_t* win, int x, int y, const char* str, uint32_t fg, uint32_t bg);
