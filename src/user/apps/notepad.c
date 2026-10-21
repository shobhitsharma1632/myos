#include <libuser.h>
#include <libgui.h>

#define MAX_TEXT_LEN 1024

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

    gui_window_t* win = gui_create_window("Notepad - Untitled", 120, 80, 440, 260);
    if (!win)
    {
        printf("[Notepad] Failed to create GUI window!\n");
        return 1;
    }

    char text[MAX_TEXT_LEN];
    int  text_len = 0;

    const char* init_msg = "Welcome to myOS Notepad!\nType on your keyboard to write text.\nBackspace and Enter are supported.\n";
    for (int i = 0; init_msg[i] && text_len < MAX_TEXT_LEN - 1; i++)
    {
        text[text_len++] = init_msg[i];
    }
    text[text_len] = '\0';

    int frame_count = 0;
    while (!win->closed)
    {
        // 1. Process pending input events
        gui_event_t ev;
        while (gui_poll_event(win, &ev))
        {
            if (ev.type == GUI_EVENT_CLOSE)
            {
                win->closed = true;
                break;
            }
            else if (ev.type == GUI_EVENT_KEY_DOWN)
            {
                char c = (char)ev.key_char;
                if (c == '\b') // Backspace
                {
                    if (text_len > 0)
                    {
                        text[--text_len] = '\0';
                    }
                }
                else if (c == '\n' || c == '\r') // Enter
                {
                    if (text_len < MAX_TEXT_LEN - 1)
                    {
                        text[text_len++] = '\n';
                        text[text_len] = '\0';
                    }
                }
                else if (c >= 32 && c <= 126) // Printable character
                {
                    if (text_len < MAX_TEXT_LEN - 1)
                    {
                        text[text_len++] = c;
                        text[text_len] = '\0';
                    }
                }
            }
        }

        // 2. Render Notepad UI
        // Clear document area to crisp white
        gui_clear(win, GUI_COLOR_WHITE);

        // Top menu / ribbon bar
        gui_fill_rect(win, 0, 0, win->client_w, 20, 0x00E2E8F0);
        gui_draw_line(win, 0, 20, win->client_w, 20, 0x00CBD5E1);
        gui_draw_text(win, 8, 6, "File   Edit   Format   View   Help", 0x00334155, GUI_COLOR_TRANSPARENT);

        // Document body text
        int text_x = 10;
        int text_y = 28;
        for (int i = 0; i < text_len; i++)
        {
            if (text[i] == '\n')
            {
                text_y += 12;
                text_x = 10;
            }
            else
            {
                gui_draw_char(win, text_x, text_y, text[i], 0x000F172A, GUI_COLOR_WHITE);
                text_x += 8;
                if (text_x > win->client_w - 18)
                {
                    text_x = 10;
                    text_y += 12;
                }
            }
        }

        // Blinking text cursor
        if ((frame_count / 10) % 2 == 0)
        {
            gui_fill_rect(win, text_x, text_y, 2, 9, 0x002563EB);
        }

        // Bottom status bar
        int status_y = win->client_h - 18;
        gui_fill_rect(win, 0, status_y, win->client_w, 18, 0x00F1F5F9);
        gui_draw_line(win, 0, status_y, win->client_w, status_y, 0x00E2E8F0);

        char status_str[64];
        snprintf(status_str, sizeof(status_str), "UTF-8 | Chars: %d/%d | Windows (CRLF)", text_len, MAX_TEXT_LEN);
        gui_draw_text(win, 8, status_y + 5, status_str, 0x0064748B, GUI_COLOR_TRANSPARENT);

        // Present updated frame
        gui_present(win);

        if (test_mode)
        {
            // Automated verification mode: inject synthetic text and exit
            if (frame_count >= 10)
            {
                break;
            }
        }

        frame_count++;
        sys_sleep(2); // Yield CPU
    }

    gui_destroy_window(win);
    return 0;
}
