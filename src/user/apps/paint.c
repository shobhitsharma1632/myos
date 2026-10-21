#include <libuser.h>
#include <libgui.h>

static const uint32_t s_PaletteColors[8] = {
    0x00EF4444, // Red
    0x0022C55E, // Green
    0x003B82F6, // Blue
    0x00EAB308, // Yellow
    0x0006B6D4, // Cyan
    0x00D946EF, // Magenta
    0x00FFFFFF, // White
    0x000F172A  // Black
};

int main(int argc, char* argv[])
{
    bool test_mode = false;
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--test") == 0)
        {
            test_mode = true;
            break;
        }
    }

    gui_window_t* win = gui_create_window("Paint - Canvas Studio", 180, 100, 420, 280);
    if (!win)
    {
        printf("[Paint] Failed to create GUI window!\n");
        return 1;
    }

    // Initialize canvas to crisp white
    gui_clear(win, GUI_COLOR_WHITE);

    uint32_t active_color = s_PaletteColors[0]; // Default Red
    int last_x = -1;
    int last_y = -1;
    bool is_painting = false;

    // Initial canvas banner
    gui_draw_text(win, 12, 12, "myOS Paint Studio", 0x00334155, GUI_COLOR_TRANSPARENT);
    gui_draw_text(win, 12, 26, "Click and drag to draw. Click swatches below to pick colors.", 0x0064748B, GUI_COLOR_TRANSPARENT);

    int frame_count = 0;
    while (!win->closed)
    {
        int palette_y = win->client_h - 32;

        gui_event_t ev;
        while (gui_poll_event(win, &ev))
        {
            if (ev.type == GUI_EVENT_CLOSE)
            {
                win->closed = true;
                break;
            }
            else if (ev.type == GUI_EVENT_MOUSE_DOWN)
            {
                // Check if clicked in color palette toolbar
                if (ev.mouse_y >= palette_y)
                {
                    // Check Clear button (first 50px)
                    if (ev.mouse_x >= 6 && ev.mouse_x <= 56)
                    {
                        gui_clear(win, GUI_COLOR_WHITE);
                    }
                    else
                    {
                        // Check swatches
                        int swatch_x = 70;
                        for (int i = 0; i < 8; i++)
                        {
                            if (ev.mouse_x >= swatch_x && ev.mouse_x <= swatch_x + 22)
                            {
                                active_color = s_PaletteColors[i];
                                break;
                            }
                            swatch_x += 28;
                        }
                    }
                }
                else
                {
                    // Started drawing on canvas
                    is_painting = true;
                    last_x = ev.mouse_x;
                    last_y = ev.mouse_y;
                    gui_fill_circle(win, ev.mouse_x, ev.mouse_y, 2, active_color);
                }
            }
            else if (ev.type == GUI_EVENT_MOUSE_UP)
            {
                is_painting = false;
                last_x = -1;
                last_y = -1;
            }
            else if (ev.type == GUI_EVENT_MOUSE_MOVE)
            {
                if (is_painting && ev.mouse_y < palette_y)
                {
                    if (last_x >= 0 && last_y >= 0)
                    {
                        gui_draw_line(win, last_x, last_y, ev.mouse_x, ev.mouse_y, active_color);
                        gui_fill_circle(win, ev.mouse_x, ev.mouse_y, 2, active_color);
                    }
                    last_x = ev.mouse_x;
                    last_y = ev.mouse_y;
                }
            }
        }

        // Draw Bottom Color Palette & Tool Bar
        gui_fill_rect(win, 0, palette_y, win->client_w, 32, 0x00E2E8F0);
        gui_draw_line(win, 0, palette_y, win->client_w, palette_y, 0x00CBD5E1);

        // "Clear" Button
        gui_fill_rect(win, 6, palette_y + 4, 52, 24, 0x00DC2626);
        gui_draw_text(win, 14, palette_y + 11, "Clear", GUI_COLOR_WHITE, GUI_COLOR_TRANSPARENT);

        // 8 Color Swatches
        int swatch_x = 70;
        for (int i = 0; i < 8; i++)
        {
            gui_fill_rect(win, swatch_x, palette_y + 5, 22, 22, s_PaletteColors[i]);
            uint32_t border = (s_PaletteColors[i] == active_color) ? 0x000F172A : 0x0094A3B8;
            gui_draw_rect(win, swatch_x, palette_y + 5, 22, 22, border);
            if (s_PaletteColors[i] == active_color)
            {
                // Active selection ring
                gui_draw_rect(win, swatch_x - 1, palette_y + 4, 24, 24, 0x002563EB);
            }
            swatch_x += 28;
        }

        gui_present(win);

        if (test_mode)
        {
            // Automated test mode: draw test patterns
            gui_draw_line(win, 50, 80, 200, 80, s_PaletteColors[0]);
            gui_draw_line(win, 50, 100, 200, 100, s_PaletteColors[1]);
            gui_draw_line(win, 50, 120, 200, 120, s_PaletteColors[2]);
            gui_draw_circle(win, 280, 100, 30, s_PaletteColors[3]);
            gui_present(win);

            if (frame_count >= 10)
            {
                break;
            }
        }

        frame_count++;
        sys_sleep(2);
    }

    gui_destroy_window(win);
    return 0;
}
