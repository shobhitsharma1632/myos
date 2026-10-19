#include "gui.h"
#include "gfx.h"
#include <arch/i686/mouse.h>
#include <arch/i686/keyboard.h>
#include <hal/task.h>
#include <hal/pmm.h>
#include <hal/vmm.h>
#include <hal/heap.h>
#include <hal/vfs.h>
#include <hal/fat.h>
#include <arch/i686/ata.h>
#include <stdio.h>
#include <debug.h>
#include <stdbool.h>
#include <stddef.h>

#define MODULE "GUI"

#define TASKBAR_HEIGHT      32
#define TITLEBAR_HEIGHT     22
#define BORDER_COLOR        0x00475569
#define SHADOW_COLOR        0x000A0E17

static uint32_t g_GuiWidth = 1024;
static uint32_t g_GuiHeight = 768;
static bool     g_GuiRunning = false;
static uint32_t g_NextWindowId = 1;

static GUI_Window* g_WindowList = NULL;
static GUI_Window* g_ActiveDragWindow = NULL;

static bool g_PrevLeftButton = false;

// Pre-declared window draw callbacks
static void Draw_SchedulerWindow(GUI_Window* win);
static void Draw_MemoryWindow(GUI_Window* win);
static void Draw_ConsoleWindow(GUI_Window* win);
static void Draw_AboutWindow(GUI_Window* win);
static void Draw_FileExplorerWindow(GUI_Window* win);

GUI_Window* GUI_CreateWindow(const char* title, int32_t x, int32_t y, int32_t width, int32_t height, GUI_DrawCallback onDraw)
{
    GUI_Window* win = (GUI_Window*)kmalloc(sizeof(GUI_Window));
    if (!win)
    {
        log_err(MODULE, "Failed to allocate memory for window '%s'", title);
        return NULL;
    }

    win->id = g_NextWindowId++;
    win->title = title;
    win->x = x;
    win->y = y;
    win->width = width;
    win->height = height;
    win->isVisible = true;
    win->isFocused = false;
    win->isDragging = false;
    win->dragOffsetX = 0;
    win->dragOffsetY = 0;
    win->titleColor = GFX_COLOR_TITLEBAR;
    win->onDraw = onDraw;
    win->userData = NULL;

    // Prepend to window list (top of Z-order)
    win->next = g_WindowList;
    g_WindowList = win;

    GUI_FocusWindow(win);
    return win;
}

void GUI_DestroyWindow(GUI_Window* win)
{
    if (!win) return;

    if (g_WindowList == win)
    {
        g_WindowList = win->next;
    }
    else
    {
        GUI_Window* prev = g_WindowList;
        while (prev && prev->next != win)
            prev = prev->next;
        if (prev)
            prev->next = win->next;
    }
    kfree(win);
}

void GUI_FocusWindow(GUI_Window* win)
{
    if (!win || !g_WindowList) return;

    // Mark all unfocused
    GUI_Window* cur = g_WindowList;
    while (cur)
    {
        cur->isFocused = false;
        cur = cur->next;
    }

    win->isFocused = true;

    // If not already head of list, move to head
    if (g_WindowList != win)
    {
        GUI_Window* prev = g_WindowList;
        while (prev && prev->next != win)
            prev = prev->next;
        if (prev)
        {
            prev->next = win->next;
            win->next = g_WindowList;
            g_WindowList = win;
        }
    }
}

static void GUI_DrawDesktop(void)
{
    // 1. Draw elegant desktop background
    GFX_Clear(GFX_COLOR_SLATE_BG);

    // Subtle desktop grid dots
    for (int32_t y = 20; y < (int32_t)(g_GuiHeight - TASKBAR_HEIGHT); y += 32)
    {
        for (int32_t x = 20; x < (int32_t)g_GuiWidth; x += 32)
        {
            GFX_PutPixel(x, y, 0x002B374E);
        }
    }

    // 2. Draw modern bottom taskbar
    int32_t barY = g_GuiHeight - TASKBAR_HEIGHT;
    GFX_FillRect(0, barY, g_GuiWidth, TASKBAR_HEIGHT, GFX_COLOR_SLATE_PANEL);
    GFX_FillRect(0, barY, g_GuiWidth, 1, 0x00475569); // Top accent border

    // Start Button
    GFX_FillRect(6, barY + 4, 76, 24, 0x000F766E);
    GFX_DrawRect(6, barY + 4, 76, 24, 0x0014B8A6);
    GFX_DrawString(16, barY + 12, "myOS", GFX_COLOR_WHITE, GFX_COLOR_TRANSPARENT);

    // Running Tasks Indicator
    int32_t taskX = 92;
    GUI_Window* cur = g_WindowList;
    while (cur)
    {
        if (cur->isVisible)
        {
            uint32_t tabColor = cur->isFocused ? 0x001E293B : 0x00334155;
            uint32_t borderColor = cur->isFocused ? 0x003B82F6 : 0x00475569;
            GFX_FillRect(taskX, barY + 4, 110, 24, tabColor);
            GFX_DrawRect(taskX, barY + 4, 110, 24, borderColor);

            // Truncate title for taskbar tab
            char tabTitle[12];
            int i = 0;
            for (; i < 11 && cur->title[i]; i++) tabTitle[i] = cur->title[i];
            tabTitle[i] = '\0';

            GFX_DrawString(taskX + 8, barY + 12, tabTitle, GFX_COLOR_WHITE, GFX_COLOR_TRANSPARENT);
            taskX += 116;
        }
        cur = cur->next;
    }

    // System Status Tray (Right side of taskbar)
    char trayStr[64];
    uint32_t freeMb = PMM_GetFreeMemory() / (1024 * 1024);
    uint32_t totalMb = PMM_GetTotalMemory() / (1024 * 1024);
    snprintf(trayStr, sizeof(trayStr), "RAM: %u/%u MB | MLFQ 100Hz", freeMb, totalMb);
    GFX_DrawString(g_GuiWidth - 260, barY + 12, trayStr, GFX_COLOR_CYAN, GFX_COLOR_TRANSPARENT);
}

static void GUI_DrawWindow(GUI_Window* win)
{
    if (!win || !win->isVisible) return;

    // 1. Drop shadow
    GFX_FillRect(win->x + 4, win->y + 4, win->width, win->height, SHADOW_COLOR);

    // 2. Window outer border
    GFX_DrawRect(win->x, win->y, win->width, win->height, BORDER_COLOR);

    // 3. Title bar
    uint32_t titleBg = win->isFocused ? GFX_COLOR_TITLEBAR : GFX_COLOR_TITLEBAR_INACTIVE;
    GFX_FillRect(win->x + 1, win->y + 1, win->width - 2, TITLEBAR_HEIGHT, titleBg);

    // Title text
    GFX_DrawString(win->x + 8, win->y + 8, win->title, GFX_COLOR_WHITE, GFX_COLOR_TRANSPARENT);

    // Close Button [X]
    int32_t closeX = win->x + win->width - 19;
    int32_t closeY = win->y + 4;
    GFX_FillRect(closeX, closeY, 15, 15, 0x00DC2626);
    GFX_DrawChar(closeX + 4, closeY + 4, 'x', GFX_COLOR_WHITE, GFX_COLOR_TRANSPARENT);

    // 4. Client background
    int32_t clientX = win->x + 1;
    int32_t clientY = win->y + TITLEBAR_HEIGHT + 1;
    int32_t clientW = win->width - 2;
    int32_t clientH = win->height - TITLEBAR_HEIGHT - 2;
    GFX_FillRect(clientX, clientY, clientW, clientH, GFX_COLOR_DARK_BLUE);

    // 5. Render client content with clipping
    if (win->onDraw)
    {
        GFX_SetClip(clientX, clientY, clientW, clientH);
        win->onDraw(win);
        GFX_ResetClip();
    }
}

// Window Content: MLFQ Scheduler Status
static void Draw_SchedulerWindow(GUI_Window* win)
{
    int32_t x = win->x + 12;
    int32_t y = win->y + TITLEBAR_HEIGHT + 12;

    GFX_DrawString(x, y, "MLFQ Preemptive Task Scheduler", GFX_COLOR_EMERALD, GFX_COLOR_TRANSPARENT);
    y += 18;

    GFX_DrawString(x, y, "Queue 0 (High Priority / Interactive - Quantum: 2 ticks):", GFX_COLOR_CYAN, GFX_COLOR_TRANSPARENT);
    y += 12;
    GFX_FillRect(x, y, win->width - 24, 8, 0x001E293B);
    GFX_FillRect(x, y, 180, 8, GFX_COLOR_CYAN); // Visual meter
    y += 16;

    GFX_DrawString(x, y, "Queue 1 (Medium Priority - Quantum: 6 ticks):", GFX_COLOR_AMBER, GFX_COLOR_TRANSPARENT);
    y += 12;
    GFX_FillRect(x, y, win->width - 24, 8, 0x001E293B);
    GFX_FillRect(x, y, 90, 8, GFX_COLOR_AMBER);
    y += 16;

    GFX_DrawString(x, y, "Queue 2 (Batch / CPU-bound - Quantum: 15 ticks):", GFX_COLOR_ROSE, GFX_COLOR_TRANSPARENT);
    y += 12;
    GFX_FillRect(x, y, win->width - 24, 8, 0x001E293B);
    GFX_FillRect(x, y, 40, 8, GFX_COLOR_ROSE);
    y += 20;

    GFX_DrawLine(x, y, x + win->width - 24, y, 0x00334155);
    y += 10;

    GFX_DrawString(x, y, "* Rule 4a: Quantum expiration causes queue demotion", GFX_COLOR_LIGHT_SLATE, GFX_COLOR_TRANSPARENT);
    y += 12;
    GFX_DrawString(x, y, "* Rule 4b: Voluntary yield preserves Queue 0 status", GFX_COLOR_LIGHT_SLATE, GFX_COLOR_TRANSPARENT);
    y += 12;
    GFX_DrawString(x, y, "* Rule 5: Anti-starvation boost elevates all every 100 ticks", GFX_COLOR_LIGHT_SLATE, GFX_COLOR_TRANSPARENT);
}

// Window Content: System Memory Diagnostics
static void Draw_MemoryWindow(GUI_Window* win)
{
    int32_t x = win->x + 12;
    int32_t y = win->y + TITLEBAR_HEIGHT + 12;

    GFX_DrawString(x, y, "Physical Memory Manager (PMM)", GFX_COLOR_AMBER, GFX_COLOR_TRANSPARENT);
    y += 14;

    uint32_t totalMb = PMM_GetTotalMemory() / (1024 * 1024);
    uint32_t freeMb = PMM_GetFreeMemory() / (1024 * 1024);
    uint32_t usedKb = PMM_GetUsedMemory() / 1024;

    char buf[64];
    snprintf(buf, sizeof(buf), "RAM: %u MB Total | %u MB Free | %u KB Used", totalMb, freeMb, usedKb);
    GFX_DrawString(x, y, buf, GFX_COLOR_WHITE, GFX_COLOR_TRANSPARENT);
    y += 12;

    // Progress bar for physical RAM
    uint32_t barW = win->width - 24;
    GFX_FillRect(x, y, barW, 10, 0x001E293B);
    uint32_t usedW = (totalMb > 0) ? ((totalMb - freeMb) * barW) / totalMb : 0;
    if (usedW < 4) usedW = 4;
    GFX_FillRect(x, y, usedW, 10, GFX_COLOR_AMBER);
    y += 20;

    GFX_DrawString(x, y, "Virtual Memory Manager (VMM)", GFX_COLOR_CYAN, GFX_COLOR_TRANSPARENT);
    y += 14;
    GFX_DrawString(x, y, "Mode: Two-Level x86 Paging (CR0.PG = 1)", GFX_COLOR_WHITE, GFX_COLOR_TRANSPARENT);
    y += 12;
    GFX_DrawString(x, y, "Mapping: 128 MB Identity | PDE 1023 Recursive Self-Map", GFX_COLOR_LIGHT_SLATE, GFX_COLOR_TRANSPARENT);
    y += 20;

    GFX_DrawString(x, y, "Kernel Dynamic Heap (Boundary-Tag)", GFX_COLOR_EMERALD, GFX_COLOR_TRANSPARENT);
    y += 14;
    snprintf(buf, sizeof(buf), "Base: 0x10000000 | Mapped: %u KB | Free: %u KB",
             (uint32_t)(KHeap_GetTotalMemory() / 1024), (uint32_t)(KHeap_GetFreeMemory() / 1024));
    GFX_DrawString(x, y, buf, GFX_COLOR_WHITE, GFX_COLOR_TRANSPARENT);
}

// Window Content: System Console / Log Viewer
static void Draw_ConsoleWindow(GUI_Window* win)
{
    int32_t x = win->x + 12;
    int32_t y = win->y + TITLEBAR_HEIGHT + 12;

    GFX_DrawString(x, y, "myOS Kernel System Log (Virtual TTY)", GFX_COLOR_CYAN, GFX_COLOR_TRANSPARENT);
    y += 16;

    GFX_DrawString(x, y, "[OK] Intel 8259 PIC initialized & IRQ 0..15 mapped", GFX_COLOR_GREEN, GFX_COLOR_TRANSPARENT);
    y += 12;
    GFX_DrawString(x, y, "[OK] Bochs Graphics Adapter initialized (1024x768 @ 32bpp)", GFX_COLOR_GREEN, GFX_COLOR_TRANSPARENT);
    y += 12;
    GFX_DrawString(x, y, "[OK] PS/2 Mouse streaming active on IRQ 12", GFX_COLOR_GREEN, GFX_COLOR_TRANSPARENT);
    y += 12;
    GFX_DrawString(x, y, "[OK] MLFQ Concurrency Engine active (100 Hz PIT timer)", GFX_COLOR_GREEN, GFX_COLOR_TRANSPARENT);
    y += 12;
    GFX_DrawString(x, y, "[OK] Double-buffered 2D Window Compositor online", GFX_COLOR_GREEN, GFX_COLOR_TRANSPARENT);
    y += 16;
    GFX_DrawString(x, y, "myos-gui> ready for user mouse and keyboard input.", GFX_COLOR_WHITE, GFX_COLOR_TRANSPARENT);
}

// Window Content: About Dialog
static void Draw_AboutWindow(GUI_Window* win)
{
    int32_t x = win->x + 12;
    int32_t y = win->y + TITLEBAR_HEIGHT + 14;

    GFX_DrawString(x, y, "myOS Desktop GUI", GFX_COLOR_EMERALD, GFX_COLOR_TRANSPARENT);
    y += 16;
    GFX_DrawString(x, y, "Version : 1.0 (Phase 10 Graphical Subsystem)", GFX_COLOR_WHITE, GFX_COLOR_TRANSPARENT);
    y += 12;
    GFX_DrawString(x, y, "Display : Bochs VBE 1024x768 @ 32bpp (LFB)", GFX_COLOR_WHITE, GFX_COLOR_TRANSPARENT);
    y += 12;
    GFX_DrawString(x, y, "Kernel  : 32-bit x86 Protected Mode + Paging", GFX_COLOR_WHITE, GFX_COLOR_TRANSPARENT);
    y += 12;
    GFX_DrawString(x, y, "Window  : Non-tearing Double-Buffered Compositor", GFX_COLOR_LIGHT_SLATE, GFX_COLOR_TRANSPARENT);
    y += 16;
    GFX_DrawString(x, y, "Click and drag any window title bar to move!", GFX_COLOR_AMBER, GFX_COLOR_TRANSPARENT);
}

// Window Content: File Explorer
static void Draw_FileExplorerWindow(GUI_Window* win)
{
    int32_t x = win->x + 12;
    int32_t y = win->y + TITLEBAR_HEIGHT + 10;

    GFX_DrawString(x, y, "VFS File Explorer: /", GFX_COLOR_CYAN, GFX_COLOR_TRANSPARENT);
    y += 16;

    GFX_DrawLine(x, y, x + win->width - 24, y, 0x00334155);
    y += 8;

    GFX_DrawString(x, y, "TYPE", GFX_COLOR_LIGHT_SLATE, GFX_COLOR_TRANSPARENT);
    GFX_DrawString(x + 55, y, "NAME", GFX_COLOR_LIGHT_SLATE, GFX_COLOR_TRANSPARENT);
    GFX_DrawString(x + 220, y, "SIZE", GFX_COLOR_LIGHT_SLATE, GFX_COLOR_TRANSPARENT);
    y += 14;

    GFX_DrawLine(x, y, x + win->width - 24, y, 0x001E293B);
    y += 8;

    VFS_DirEntry entry;
    size_t index = 0;
    int items = 0;

    while (vfs_readdir("/", index, &entry) == 0 && items < 8)
    {
        if (entry.is_directory)
        {
            GFX_FillRect(x, y - 1, 42, 12, 0x00854D0E);
            GFX_DrawString(x + 4, y, "[DIR]", GFX_COLOR_AMBER, GFX_COLOR_TRANSPARENT);
            GFX_DrawString(x + 55, y, entry.name, GFX_COLOR_WHITE, GFX_COLOR_TRANSPARENT);
            GFX_DrawString(x + 220, y, "<DIR>", GFX_COLOR_LIGHT_SLATE, GFX_COLOR_TRANSPARENT);
        }
        else
        {
            GFX_FillRect(x, y - 1, 42, 12, 0x00065F46);
            GFX_DrawString(x + 4, y, "[FILE]", GFX_COLOR_EMERALD, GFX_COLOR_TRANSPARENT);
            GFX_DrawString(x + 55, y, entry.name, GFX_COLOR_WHITE, GFX_COLOR_TRANSPARENT);

            char sizeBuf[32];
            snprintf(sizeBuf, sizeof(sizeBuf), "%u bytes", entry.size);
            GFX_DrawString(x + 220, y, sizeBuf, GFX_COLOR_CYAN, GFX_COLOR_TRANSPARENT);
        }

        y += 16;
        index++;
        items++;
    }

    // Bottom status bar in explorer
    int32_t statusY = win->y + win->height - 24;
    GFX_FillRect(win->x + 2, statusY, win->width - 4, 22, 0x00111827);
    GFX_DrawLine(win->x + 2, statusY, win->x + win->width - 2, statusY, 0x00334155);

    const ATA_DeviceInfo* dev = ATA_GetDevice();
    char devInfo[64];
    if (dev && dev->present)
    {
        snprintf(devInfo, sizeof(devInfo), "Storage: ATA Primary Master (%u MB) | FAT12", dev->total_mb);
    }
    else
    {
        snprintf(devInfo, sizeof(devInfo), "Storage: No device");
    }
    GFX_DrawString(win->x + 10, statusY + 5, devInfo, GFX_COLOR_LIGHT_SLATE, GFX_COLOR_TRANSPARENT);
}

void GUI_Render(void)
{
    // 1. Draw desktop background & taskbar
    GUI_DrawDesktop();

    // 2. Draw windows in reverse Z-order (bottom to top)
    // Build temporary array of windows from head to tail
    GUI_Window* wins[16];
    int count = 0;
    GUI_Window* cur = g_WindowList;
    while (cur && count < 16)
    {
        wins[count++] = cur;
        cur = cur->next;
    }

    // Render from back (wins[count - 1]) to front (wins[0])
    for (int i = count - 1; i >= 0; i--)
    {
        GUI_DrawWindow(wins[i]);
    }

    // 3. Process mouse interaction and draw cursor
    MouseState mouse;
    Mouse_GetState(&mouse);

    // Mouse click handling
    bool leftClickDown = mouse.leftButton && !g_PrevLeftButton;
    bool leftClickUp = !mouse.leftButton && g_PrevLeftButton;
    g_PrevLeftButton = mouse.leftButton;

    if (leftClickDown)
    {
        // Check windows from top to bottom
        for (int i = 0; i < count; i++)
        {
            GUI_Window* w = wins[i];
            if (!w->isVisible) continue;

            // Check close button click
            int32_t closeX = w->x + w->width - 19;
            int32_t closeY = w->y + 4;
            if (mouse.x >= closeX && mouse.x <= closeX + 15 &&
                mouse.y >= closeY && mouse.y <= closeY + 15)
            {
                w->isVisible = false;
                break;
            }

            // Check title bar drag click
            if (mouse.x >= w->x && mouse.x <= w->x + w->width &&
                mouse.y >= w->y && mouse.y <= w->y + TITLEBAR_HEIGHT)
            {
                GUI_FocusWindow(w);
                w->isDragging = true;
                w->dragOffsetX = mouse.x - w->x;
                w->dragOffsetY = mouse.y - w->y;
                g_ActiveDragWindow = w;
                break;
            }

            // Check general window click for focus
            if (mouse.x >= w->x && mouse.x <= w->x + w->width &&
                mouse.y >= w->y && mouse.y <= w->y + w->height)
            {
                GUI_FocusWindow(w);
                break;
            }
        }
    }

    // Active drag update
    if (g_ActiveDragWindow)
    {
        if (mouse.leftButton)
        {
            g_ActiveDragWindow->x = mouse.x - g_ActiveDragWindow->dragOffsetX;
            g_ActiveDragWindow->y = mouse.y - g_ActiveDragWindow->dragOffsetY;

            // Clamp so title bar cannot be dragged off-screen
            if (g_ActiveDragWindow->x < -((int32_t)g_ActiveDragWindow->width - 40))
                g_ActiveDragWindow->x = -((int32_t)g_ActiveDragWindow->width - 40);
            if (g_ActiveDragWindow->x > (int32_t)g_GuiWidth - 40)
                g_ActiveDragWindow->x = (int32_t)g_GuiWidth - 40;
            if (g_ActiveDragWindow->y < 0)
                g_ActiveDragWindow->y = 0;
            if (g_ActiveDragWindow->y > (int32_t)(g_GuiHeight - TASKBAR_HEIGHT - TITLEBAR_HEIGHT))
                g_ActiveDragWindow->y = (int32_t)(g_GuiHeight - TASKBAR_HEIGHT - TITLEBAR_HEIGHT);
        }
        else
        {
            g_ActiveDragWindow->isDragging = false;
            g_ActiveDragWindow = NULL;
        }
    }

    // 4. Render mouse pointer at latest coordinates
    GFX_DrawCursor(mouse.x, mouse.y);

    // 5. Blit backbuffer to front Linear Framebuffer
    GFX_SwapBuffers();
}

void GUI_Task(void)
{
    log_info(MODULE, "GUI Compositor task started in MLFQ Queue 0.");

    while (g_GuiRunning)
    {
        GUI_Render();

        // Yield CPU cooperatively so MLFQ background tasks compute
        Task_Yield();
    }
}

bool GUI_Initialize(uint32_t width, uint32_t height)
{
    g_GuiWidth = width;
    g_GuiHeight = height;

    // 1. Initialize 2D Graphics Engine with BGA 1024x768 @ 32bpp
    if (!GFX_Initialize(width, height))
    {
        log_err(MODULE, "GFX_Initialize failed!");
        return false;
    }

    // 2. Initialize PS/2 Mouse Driver
    i686_Mouse_Initialize(width, height);

    // 3. Create default desktop windows
    GUI_CreateWindow("MLFQ Scheduler", 30, 30, 460, 230, Draw_SchedulerWindow);
    GUI_CreateWindow("System Memory", 520, 30, 460, 230, Draw_MemoryWindow);
    GUI_CreateWindow("Terminal Log", 30, 280, 460, 250, Draw_ConsoleWindow);
    GUI_CreateWindow("About myOS", 520, 280, 460, 250, Draw_AboutWindow);
    GUI_CreateWindow("File Explorer (/)", 240, 160, 520, 300, Draw_FileExplorerWindow);

    g_GuiRunning = true;
    log_info(MODULE, "GUI Subsystem successfully initialized!");
    return true;
}

void GUI_Start(void)
{
    Task_Create("gui_compositor", GUI_Task);
}

bool GUI_IsRunning(void)
{
    return g_GuiRunning;
}
