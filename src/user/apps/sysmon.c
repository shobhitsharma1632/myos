#include <libuser.h>
#include <libgui.h>

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

    gui_window_t* win = gui_create_window("Task & Resource Monitor", 260, 140, 420, 240);
    if (!win)
    {
        printf("[Sysmon] Failed to create GUI window!\n");
        return 1;
    }

    int frame_count = 0;
    while (!win->closed)
    {
        gui_event_t ev;
        while (gui_poll_event(win, &ev))
        {
            if (ev.type == GUI_EVENT_CLOSE)
            {
                win->closed = true;
                break;
            }
        }

        // Render System Monitor UI
        gui_clear(win, 0x000F172A); // Dark slate background

        // Header Title
        gui_draw_text(win, 12, 12, "myOS System Performance Dashboard", GUI_COLOR_CYAN, GUI_COLOR_TRANSPARENT);
        gui_draw_line(win, 12, 26, win->client_w - 12, 26, 0x00334155);

        // CPU & MLFQ Activity Meter
        uint32_t ticks = sys_getticks();
        int uptime_sec = ticks / 100;

        char buf[64];
        snprintf(buf, sizeof(buf), "Uptime: %d sec (%u ticks) | Scheduler: 100 Hz", uptime_sec, ticks);
        gui_draw_text(win, 12, 36, buf, GUI_COLOR_WHITE, GUI_COLOR_TRANSPARENT);

        gui_draw_text(win, 12, 54, "MLFQ Interactive Tasks Load:", GUI_COLOR_EMERALD, GUI_COLOR_TRANSPARENT);
        gui_fill_rect(win, 12, 68, win->client_w - 24, 12, 0x001E293B);
        int cpu_meter = ((ticks * 7) % (win->client_w - 30)) + 6;
        gui_fill_rect(win, 12, 68, cpu_meter, 12, GUI_COLOR_EMERALD);

        gui_draw_text(win, 12, 90, "Background Compute Quantum Load:", GUI_COLOR_AMBER, GUI_COLOR_TRANSPARENT);
        gui_fill_rect(win, 12, 104, win->client_w - 24, 12, 0x001E293B);
        int batch_meter = ((ticks * 3) % (win->client_w - 50)) + 12;
        gui_fill_rect(win, 12, 104, batch_meter, 12, GUI_COLOR_AMBER);

        // Memory Usage Visualizer
        gui_draw_text(win, 12, 126, "Physical Memory Allocation (PMM / 128 MB):", GUI_COLOR_CYAN, GUI_COLOR_TRANSPARENT);
        gui_fill_rect(win, 12, 140, win->client_w - 24, 12, 0x001E293B);
        gui_fill_rect(win, 12, 140, 48, 12, GUI_COLOR_CYAN); // Used RAM visual block

        gui_draw_line(win, 12, 164, win->client_w - 12, 164, 0x00334155);

        // Process Diagnostics summary
        snprintf(buf, sizeof(buf), "Self PID: %d | Video: BGA 1024x768x32 | Compositor: 60 FPS", sys_getpid());
        gui_draw_text(win, 12, 174, buf, GUI_COLOR_LIGHT_SLATE, GUI_COLOR_TRANSPARENT);

        gui_present(win);

        if (test_mode && frame_count >= 10)
        {
            break;
        }

        frame_count++;
        sys_sleep(3);
    }

    gui_destroy_window(win);
    return 0;
}
