#include "gui.h"
#include "gfx.h"
#include <arch/i686/mouse.h>
#include <arch/i686/keyboard.h>
#include <arch/i686/bga.h>
#include <hal/spinlock.h>
#include <hal/task.h>
#include <hal/pmm.h>
#include <hal/vmm.h>
#include <hal/heap.h>
#include <hal/vfs.h>
#include <hal/fat.h>
#include <arch/i686/ata.h>
#include <stdio.h>
#include <string.h>
#include <memory.h>
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

static bool    g_PrevLeftButton = false;
static int32_t g_PrevMouseX = -1;
static int32_t g_PrevMouseY = -1;

static Spinlock g_GuiLock = SPINLOCK_INIT;
static bool     g_GuiDirty = true;
static bool     g_DesktopNeedsRedraw = true;
static int32_t  g_DirtyMinX = 0;
static int32_t  g_DirtyMinY = 0;
static int32_t  g_DirtyMaxX = 1024;
static int32_t  g_DirtyMaxY = 768;

static bool g_StartMenuOpen = false;
static char g_AppSearchQuery[32] = "";
static int  g_AppSearchSelected = 0;
static bool g_FileExplorerSearchActive = false;
static char s_FileFilter[32] = "";

typedef struct {
    const char* name;
    const char* category;
    const char* description;
    const char* elf_path;
    const char* win_title;
    uint32_t    badge_color;
} AppLauncherItem;

static const AppLauncherItem s_AppCatalog[] = {
    { "DOOM (3D Raycaster)", "GAME", "3D retro raycasting action game",       "/bin/doom.elf",    NULL,                0x00DC2626 },
    { "Tetris",              "GAME", "Classic falling tetromino puzzle",      "/bin/tetris.elf",  NULL,                0x002563EB },
    { "Snake",               "GAME", "Retro arcade snake game",               "/bin/snake.elf",   NULL,                0x00059669 },
    { "Terminal / Shell",    "TERM", "myOS interactive command shell",        "/bin/sh.elf",      NULL,                0x00D97706 },
    { "File Explorer",       "FILE", "Browse VFS, disk sectors & directories",NULL,               "File Explorer (/)", 0x000891B2 },
    { "Terminal Log",        "TERM", "System console log & kernel output",    NULL,               "Terminal Log",      0x00D97706 },
    { "Notepad",             "APPS", "Graphical text editor & scratchpad",    "/bin/notepad.elf", NULL,                0x007C3AED },
    { "Paint",               "APPS", "Graphical canvas drawing application",  "/bin/paint.elf",   NULL,                0x00DB2777 },
    { "System Monitor",      "SYS",  "Real-time CPU & memory resource stats", "/bin/sysmon.elf",  NULL,                0x000D9488 },
    { "MLFQ Scheduler",      "SYS",  "Multi-core 3-level queue visualizer",   NULL,               "MLFQ Scheduler",    0x00059669 },
    { "System Memory",       "SYS",  "Physical & virtual memory inspector",   NULL,               "System Memory",     0x004F46E5 },
    { "Sound Blaster Audio", "APPS", "SB16 16-bit PCM wav audio player",      "/bin/play.elf",    NULL,                0x00CA8A04 },
    { "Calculator",          "APPS", "Arithmetic command calculator",         "/bin/calc.elf",    NULL,                0x00475569 },
    { "SMP Topology",        "SYS",  "CPU cores & Local APIC report",         "/bin/smpinfo.elf", NULL,                0x000891B2 },
    { "About myOS",          "SYS",  "OS kernel architecture & build info",   NULL,               "About myOS",        0x0064748B },
};
#define APP_CATALOG_SIZE (sizeof(s_AppCatalog) / sizeof(s_AppCatalog[0]))

static inline char to_lower(char c)
{
    if (c >= 'A' && c <= 'Z') return c + 32;
    return c;
}

static bool str_contains_case_insensitive(const char* haystack, const char* needle)
{
    if (!haystack || !needle) return false;
    if (needle[0] == '\0') return true;

    for (size_t i = 0; haystack[i] != '\0'; i++)
    {
        size_t j = 0;
        while (needle[j] != '\0' && haystack[i + j] != '\0' &&
               to_lower(haystack[i + j]) == to_lower(needle[j]))
        {
            j++;
        }
        if (needle[j] == '\0')
            return true;
    }
    return false;
}

static void MarkRegionDirty(int32_t x, int32_t y, int32_t w, int32_t h)
{
    if (w <= 0 || h <= 0) return;
    int32_t x1 = x + w;
    int32_t y1 = y + h;

    uint8_t flags = spinlock_acquire_irqsave(&g_GuiLock);
    if (!g_GuiDirty)
    {
        g_DirtyMinX = x;
        g_DirtyMinY = y;
        g_DirtyMaxX = x1;
        g_DirtyMaxY = y1;
        g_GuiDirty = true;
    }
    else
    {
        if (x < g_DirtyMinX) g_DirtyMinX = x;
        if (y < g_DirtyMinY) g_DirtyMinY = y;
        if (x1 > g_DirtyMaxX) g_DirtyMaxX = x1;
        if (y1 > g_DirtyMaxY) g_DirtyMaxY = y1;
    }
    if (g_DirtyMinX < 0) g_DirtyMinX = 0;
    if (g_DirtyMinY < 0) g_DirtyMinY = 0;
    if (g_DirtyMaxX > (int32_t)g_GuiWidth) g_DirtyMaxX = g_GuiWidth;
    if (g_DirtyMaxY > (int32_t)g_GuiHeight) g_DirtyMaxY = g_GuiHeight;
    spinlock_release_irqrestore(&g_GuiLock, flags);
}

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
        log_err(MODULE, "Failed to allocate memory for window '%s'", title ? title : "unnamed");
        return NULL;
    }
    memset(win, 0, sizeof(GUI_Window));

    win->id = g_NextWindowId++;
    win->owner_pid = 0; // Kernel internal window
    if (title)
    {
        strncpy(win->title, title, sizeof(win->title) - 1);
        win->title[sizeof(win->title) - 1] = '\0';
    }
    else
    {
        strcpy(win->title, "Window");
    }

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

    win->phys_buffer = NULL;
    win->user_virt_buffer = 0;
    win->buffer_pages = 0;
    win->client_w = width - 2;
    win->client_h = height - TITLEBAR_HEIGHT - 2;
    win->is_damaged = true;

    // Prepend to window list (top of Z-order)
    win->next = g_WindowList;
    g_WindowList = win;

    GUI_FocusWindow(win);
    return win;
}

GUI_Window* GUI_CreateClientWindow(uint32_t owner_pid, const char* title, int32_t x, int32_t y, int32_t width, int32_t height)
{
    // Auto-initialize GUI subsystem if not yet running
    if (!g_GuiRunning && BGA_IsAvailable())
    {
        log_info(MODULE, "Auto-initializing GUI subsystem for client window...");
        if (GUI_Initialize(1024, 768))
        {
            GUI_Start();
        }
    }

    if (width < 64) width = 64;
    if (width > (int32_t)g_GuiWidth) width = g_GuiWidth;
    if (height < 64) height = 64;
    if (height > (int32_t)(g_GuiHeight - TASKBAR_HEIGHT)) height = g_GuiHeight - TASKBAR_HEIGHT;

    int32_t clientW = width - 2;
    int32_t clientH = height - TITLEBAR_HEIGHT - 2;
    size_t buf_bytes = (size_t)clientW * clientH * sizeof(uint32_t);
    size_t pages_needed = (buf_bytes + PAGE_SIZE - 1) / PAGE_SIZE;

    void* phys_buf = PMM_AllocatePages(pages_needed);
    if (!phys_buf)
    {
        log_err(MODULE, "Failed to allocate %u physical frames for client window '%s'",
                (uint32_t)pages_needed, title ? title : "Window");
        return NULL;
    }
    memset(phys_buf, 0, pages_needed * PAGE_SIZE);

    GUI_Window* win = (GUI_Window*)kmalloc(sizeof(GUI_Window));
    if (!win)
    {
        PMM_FreePages(phys_buf, pages_needed);
        log_err(MODULE, "Failed to allocate TCB for client window '%s'", title ? title : "Window");
        return NULL;
    }
    memset(win, 0, sizeof(GUI_Window));

    win->id = g_NextWindowId++;
    win->owner_pid = owner_pid;
    if (title)
    {
        strncpy(win->title, title, sizeof(win->title) - 1);
        win->title[sizeof(win->title) - 1] = '\0';
    }
    else
    {
        strcpy(win->title, "Client Window");
    }

    // Default positioning if negative
    if (x < 0) x = 60 + ((win->id * 30) % 300);
    if (y < 0) y = 40 + ((win->id * 25) % 200);

    win->x = x;
    win->y = y;
    win->width = width;
    win->height = height;
    win->isVisible = true;
    win->isFocused = false;
    win->isDragging = false;
    win->titleColor = GFX_COLOR_TITLEBAR;
    win->onDraw = NULL;
    win->userData = NULL;

    win->phys_buffer = phys_buf;
    win->buffer_pages = pages_needed;
    win->client_w = clientW;
    win->client_h = clientH;
    win->is_damaged = true;

    uint32_t user_virt = USER_GUI_BUFFER_BASE + (win->id * 0x00400000);
    win->user_virt_buffer = user_virt;

    // Map each physical page into current user task's active page directory
    for (size_t p = 0; p < pages_needed; p++)
    {
        void* vaddr = (void*)(user_virt + p * PAGE_SIZE);
        void* paddr = (void*)((uint32_t)phys_buf + p * PAGE_SIZE);
        VMM_MapPage(vaddr, paddr, PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
    }

    win->next = g_WindowList;
    g_WindowList = win;

    GUI_FocusWindow(win);

    log_info(MODULE, "Created client window '%s' (ID %u, PID %u) %dx%d (virt=0x%x, phys=0x%x, pages=%u)",
             win->title, win->id, win->owner_pid, win->client_w, win->client_h,
             win->user_virt_buffer, (uint32_t)win->phys_buffer, (uint32_t)pages_needed);

    return win;
}

GUI_Window* GUI_FindWindowById(uint32_t id)
{
    GUI_Window* cur = g_WindowList;
    while (cur)
    {
        if (cur->id == id)
            return cur;
        cur = cur->next;
    }
    return NULL;
}

GUI_Window* GUI_FindWindowByTitle(const char* title)
{
    if (!title) return NULL;
    GUI_Window* cur = g_WindowList;
    while (cur)
    {
        if (strcmp(cur->title, title) == 0)
            return cur;
        cur = cur->next;
    }
    return NULL;
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

    if (g_ActiveDragWindow == win)
        g_ActiveDragWindow = NULL;

    if (win->phys_buffer && win->buffer_pages > 0)
    {
        PMM_FreePages(win->phys_buffer, win->buffer_pages);
        win->phys_buffer = NULL;
    }

    g_DesktopNeedsRedraw = true;
    MarkRegionDirty(0, 0, g_GuiWidth, g_GuiHeight);

    kfree(win);
}

void GUI_FocusWindow(GUI_Window* win)
{
    if (!win || !g_WindowList) return;

    // Mark all unfocused
    GUI_Window* cur = g_WindowList;
    while (cur)
    {
        if (cur->isFocused && cur != win && cur->owner_pid > 0)
        {
            gui_event_t ev;
            memset(&ev, 0, sizeof(ev));
            ev.type = GUI_EVENT_BLUR;
            ev.window_id = cur->id;
            GUI_EnqueueEvent(cur, &ev);
        }
        cur->isFocused = false;
        cur = cur->next;
    }

    win->isFocused = true;
    if (win->owner_pid > 0)
    {
        gui_event_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = GUI_EVENT_FOCUS;
        ev.window_id = win->id;
        GUI_EnqueueEvent(win, &ev);
    }

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
            g_DesktopNeedsRedraw = true;
            MarkRegionDirty(0, 0, g_GuiWidth, g_GuiHeight);
        }
    }
}

bool GUI_EnqueueEvent(GUI_Window* win, const gui_event_t* ev)
{
    if (!win || !ev) return false;

    if (win->events.count >= GUI_MAX_EVENTS)
    {
        // Drop oldest to make room
        win->events.tail = (win->events.tail + 1) % GUI_MAX_EVENTS;
        win->events.count--;
    }

    win->events.events[win->events.head] = *ev;
    win->events.head = (win->events.head + 1) % GUI_MAX_EVENTS;
    win->events.count++;
    return true;
}

bool GUI_DequeueEvent(GUI_Window* win, gui_event_t* ev)
{
    if (!win || !ev || win->events.count == 0) return false;

    *ev = win->events.events[win->events.tail];
    win->events.tail = (win->events.tail + 1) % GUI_MAX_EVENTS;
    win->events.count--;
    return true;
}

void GUI_DamageWindow(GUI_Window* win, int32_t x, int32_t y, int32_t w, int32_t h)
{
    if (win)
    {
        win->is_damaged = true;
        int32_t clientX = win->x + 1;
        int32_t clientY = win->y + TITLEBAR_HEIGHT + 1;
        if (w <= 0 || h <= 0)
        {
            w = win->client_w;
            h = win->client_h;
        }
        MarkRegionDirty(clientX + x, clientY + y, w, h);
    }
}

void GUI_CleanupProcessWindows(uint32_t pid)
{
    if (pid == 0) return;

    GUI_Window* cur = g_WindowList;
    while (cur)
    {
        GUI_Window* next = cur->next;
        if (cur->owner_pid == pid)
        {
            log_info(MODULE, "Cleaning up client window '%s' (ID %u) for terminated PID %u",
                     cur->title, cur->id, pid);
            GUI_DestroyWindow(cur);
        }
        cur = next;
    }
}

static int GetMatchingCatalogIndices(int* outIndices, int maxResults)
{
    int count = 0;
    for (int i = 0; i < (int)APP_CATALOG_SIZE && count < maxResults; i++)
    {
        const AppLauncherItem* item = &s_AppCatalog[i];
        if (g_AppSearchQuery[0] == '\0' ||
            str_contains_case_insensitive(item->name, g_AppSearchQuery) ||
            str_contains_case_insensitive(item->category, g_AppSearchQuery) ||
            str_contains_case_insensitive(item->description, g_AppSearchQuery))
        {
            outIndices[count++] = i;
        }
    }
    return count;
}

static void GUI_LaunchCatalogItem(int catalogIdx)
{
    if (catalogIdx < 0 || catalogIdx >= (int)APP_CATALOG_SIZE) return;
    const AppLauncherItem* item = &s_AppCatalog[catalogIdx];

    if (item->win_title)
    {
        GUI_Window* w = GUI_FindWindowByTitle(item->win_title);
        if (w)
        {
            w->isVisible = true;
            GUI_FocusWindow(w);
        }
        else
        {
            if (strcmp(item->win_title, "File Explorer (/)") == 0)
                GUI_CreateWindow("File Explorer (/)", 240, 160, 520, 300, Draw_FileExplorerWindow);
            else if (strcmp(item->win_title, "Terminal Log") == 0)
                GUI_CreateWindow("Terminal Log", 30, 280, 460, 250, Draw_ConsoleWindow);
            else if (strcmp(item->win_title, "MLFQ Scheduler") == 0)
                GUI_CreateWindow("MLFQ Scheduler", 30, 30, 460, 230, Draw_SchedulerWindow);
            else if (strcmp(item->win_title, "System Memory") == 0)
                GUI_CreateWindow("System Memory", 520, 30, 460, 230, Draw_MemoryWindow);
            else if (strcmp(item->win_title, "About myOS") == 0)
                GUI_CreateWindow("About myOS", 520, 280, 460, 250, Draw_AboutWindow);
        }
    }
    else if (item->elf_path)
    {
        char* argv[] = { (char*)item->elf_path, NULL };
        Task_CreateUserFromELF(item->name, item->elf_path, 1, argv);
    }

    g_StartMenuOpen = false;
    g_DesktopNeedsRedraw = true;
    MarkRegionDirty(0, 0, g_GuiWidth, g_GuiHeight);
}

static void GUI_DrawStartMenu(void)
{
    int32_t barY = g_GuiHeight - TASKBAR_HEIGHT;
    int32_t menuX = 6;
    int32_t menuW = 390;
    int32_t menuH = 370;
    int32_t menuY = barY - menuH - 4;

    // 1. Drop shadow
    GFX_FillRect(menuX + 4, menuY + 4, menuW, menuH, SHADOW_COLOR);

    // 2. Main background & border
    GFX_FillRect(menuX, menuY, menuW, menuH, 0x000F172A);
    GFX_DrawRect(menuX, menuY, menuW, menuH, 0x00334155);
    GFX_DrawRect(menuX + 1, menuY + 1, menuW - 2, menuH - 2, 0x001E293B);

    // 3. Header title bar
    GFX_FillRect(menuX + 2, menuY + 2, menuW - 4, 28, 0x001E293B);
    GFX_DrawString(menuX + 12, menuY + 10, "myOS Applications & Launcher", GFX_COLOR_CYAN, GFX_COLOR_TRANSPARENT);

    // 4. Search input box
    int32_t boxX = menuX + 12;
    int32_t boxY = menuY + 36;
    int32_t boxW = menuW - 24;
    int32_t boxH = 26;

    GFX_FillRect(boxX, boxY, boxW, boxH, 0x00111827);
    GFX_DrawRect(boxX, boxY, boxW, boxH, 0x003B82F6);
    GFX_DrawString(boxX + 6, boxY + 7, "[?]", GFX_COLOR_CYAN, GFX_COLOR_TRANSPARENT);

    if (g_AppSearchQuery[0] == '\0')
    {
        GFX_DrawString(boxX + 28, boxY + 7, "Type to search (games, term, files)...", GFX_COLOR_LIGHT_SLATE, GFX_COLOR_TRANSPARENT);
    }
    else
    {
        char searchDisp[40];
        snprintf(searchDisp, sizeof(searchDisp), "%s_", g_AppSearchQuery);
        GFX_DrawString(boxX + 28, boxY + 7, searchDisp, GFX_COLOR_WHITE, GFX_COLOR_TRANSPARENT);
    }

    // 5. Results listing
    int matches[16];
    int matchCount = GetMatchingCatalogIndices(matches, 16);

    char countStr[48];
    if (g_AppSearchQuery[0] == '\0')
    {
        snprintf(countStr, sizeof(countStr), "All Applications (%d available):", matchCount);
    }
    else
    {
        snprintf(countStr, sizeof(countStr), "Search Results (%d found for \"%s\"):", matchCount, g_AppSearchQuery);
    }
    GFX_DrawString(menuX + 14, menuY + 68, countStr, GFX_COLOR_LIGHT_SLATE, GFX_COLOR_TRANSPARENT);

    if (matchCount == 0)
    {
        GFX_DrawString(menuX + 20, menuY + 110, "No matching applications found.", GFX_COLOR_AMBER, GFX_COLOR_TRANSPARENT);
        GFX_DrawString(menuX + 20, menuY + 130, "Try searching: doom, tetris, sh, file", GFX_COLOR_LIGHT_SLATE, GFX_COLOR_TRANSPARENT);
    }
    else
    {
        int maxVisible = 6;
        for (int idx = 0; idx < matchCount && idx < maxVisible; idx++)
        {
            int catIdx = matches[idx];
            const AppLauncherItem* item = &s_AppCatalog[catIdx];
            int32_t rowY = menuY + 86 + (idx * 41);

            bool isSel = (idx == g_AppSearchSelected);
            if (isSel)
            {
                GFX_FillRect(menuX + 8, rowY, menuW - 16, 38, 0x001E3A8A);
                GFX_DrawRect(menuX + 8, rowY, menuW - 16, 38, 0x003B82F6);
            }
            else
            {
                GFX_FillRect(menuX + 8, rowY, menuW - 16, 38, 0x00141E33);
                GFX_DrawRect(menuX + 8, rowY, menuW - 16, 38, 0x001E293B);
            }

            // Category badge
            GFX_FillRect(menuX + 14, rowY + 6, 44, 14, item->badge_color);
            GFX_DrawString(menuX + 16, rowY + 8, item->category, GFX_COLOR_WHITE, GFX_COLOR_TRANSPARENT);

            // Name
            uint32_t nameCol = isSel ? GFX_COLOR_CYAN : GFX_COLOR_WHITE;
            GFX_DrawString(menuX + 64, rowY + 6, item->name, nameCol, GFX_COLOR_TRANSPARENT);

            // Description
            GFX_DrawString(menuX + 64, rowY + 20, item->description, GFX_COLOR_LIGHT_SLATE, GFX_COLOR_TRANSPARENT);
        }
    }

    // 6. Footer instructions
    int32_t footY = menuY + menuH - 24;
    GFX_FillRect(menuX + 2, footY, menuW - 4, 22, 0x00111827);
    GFX_DrawLine(menuX + 2, footY, menuX + menuW - 2, footY, 0x001E293B);
    GFX_DrawString(menuX + 12, footY + 6, "[Click] or [Enter]: Launch  |  [Esc]: Close", GFX_COLOR_LIGHT_SLATE, GFX_COLOR_TRANSPARENT);
}

static void GUI_HandleStartMenuKey(const KeyboardEvent* kev)
{
    if (kev->ascii == 27) // Escape
    {
        g_StartMenuOpen = false;
        g_DesktopNeedsRedraw = true;
        MarkRegionDirty(0, 0, g_GuiWidth, g_GuiHeight);
        return;
    }
    else if (kev->ascii == '\n' || kev->ascii == '\r') // Enter
    {
        int matches[16];
        int count = GetMatchingCatalogIndices(matches, 16);
        if (count > 0 && g_AppSearchSelected >= 0 && g_AppSearchSelected < count)
        {
            GUI_LaunchCatalogItem(matches[g_AppSearchSelected]);
        }
        return;
    }
    else if (kev->ascii == '\b') // Backspace
    {
        size_t len = strlen(g_AppSearchQuery);
        if (len > 0)
        {
            g_AppSearchQuery[len - 1] = '\0';
            g_AppSearchSelected = 0;
            g_DesktopNeedsRedraw = true;
            MarkRegionDirty(0, 0, g_GuiWidth, g_GuiHeight);
        }
        return;
    }
    else if (kev->scancode == 0xC8 || kev->scancode == 0x48) // Up arrow
    {
        if (g_AppSearchSelected > 0)
        {
            g_AppSearchSelected--;
            g_DesktopNeedsRedraw = true;
            MarkRegionDirty(0, 0, g_GuiWidth, g_GuiHeight);
        }
        return;
    }
    else if (kev->scancode == 0xD0 || kev->scancode == 0x50) // Down arrow
    {
        int matches[16];
        int count = GetMatchingCatalogIndices(matches, 16);
        if (g_AppSearchSelected < count - 1)
        {
            g_AppSearchSelected++;
            g_DesktopNeedsRedraw = true;
            MarkRegionDirty(0, 0, g_GuiWidth, g_GuiHeight);
        }
        return;
    }
    else if (kev->ascii >= 32 && kev->ascii <= 126) // Printable char
    {
        size_t len = strlen(g_AppSearchQuery);
        if (len < sizeof(g_AppSearchQuery) - 1)
        {
            g_AppSearchQuery[len] = kev->ascii;
            g_AppSearchQuery[len + 1] = '\0';
            g_AppSearchSelected = 0;
            g_DesktopNeedsRedraw = true;
            MarkRegionDirty(0, 0, g_GuiWidth, g_GuiHeight);
        }
    }
}

static void GUI_HandleFileExplorerKey(const KeyboardEvent* kev)
{
    if (kev->ascii == 27) // Escape
    {
        g_FileExplorerSearchActive = false;
        s_FileFilter[0] = '\0';
        g_DesktopNeedsRedraw = true;
        MarkRegionDirty(0, 0, g_GuiWidth, g_GuiHeight);
    }
    else if (kev->ascii == '\b') // Backspace
    {
        size_t len = strlen(s_FileFilter);
        if (len > 0)
        {
            s_FileFilter[len - 1] = '\0';
            g_DesktopNeedsRedraw = true;
            MarkRegionDirty(0, 0, g_GuiWidth, g_GuiHeight);
        }
    }
    else if (kev->ascii >= 32 && kev->ascii <= 126) // Printable char
    {
        size_t len = strlen(s_FileFilter);
        if (len < sizeof(s_FileFilter) - 1)
        {
            s_FileFilter[len] = kev->ascii;
            s_FileFilter[len + 1] = '\0';
            g_DesktopNeedsRedraw = true;
            MarkRegionDirty(0, 0, g_GuiWidth, g_GuiHeight);
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
    uint32_t startBg = g_StartMenuOpen ? 0x000D9488 : 0x000F766E;
    GFX_FillRect(6, barY + 4, 76, 24, startBg);
    GFX_DrawRect(6, barY + 4, 76, 24, 0x0014B8A6);
    GFX_DrawString(14, barY + 12, "[+] myOS", GFX_COLOR_WHITE, GFX_COLOR_TRANSPARENT);

    // Taskbar Search Bar
    int32_t sbX = 88;
    int32_t sbW = 200;
    GFX_FillRect(sbX, barY + 4, sbW, 24, 0x000F172A);
    GFX_DrawRect(sbX, barY + 4, sbW, 24, g_StartMenuOpen ? 0x003B82F6 : 0x00334155);
    GFX_DrawString(sbX + 8, barY + 12, "[?]", GFX_COLOR_CYAN, GFX_COLOR_TRANSPARENT);
    if (g_AppSearchQuery[0] != '\0')
    {
        char disp[32];
        snprintf(disp, sizeof(disp), "%s_", g_AppSearchQuery);
        GFX_DrawString(sbX + 28, barY + 12, disp, GFX_COLOR_WHITE, GFX_COLOR_TRANSPARENT);
    }
    else
    {
        GFX_DrawString(sbX + 28, barY + 12, "Search apps (/)...", GFX_COLOR_LIGHT_SLATE, GFX_COLOR_TRANSPARENT);
    }

    // Running Tasks Indicator
    int32_t taskX = 296;
    GUI_Window* cur = g_WindowList;
    while (cur)
    {
        if (cur->isVisible)
        {
            uint32_t tabColor = cur->isFocused ? 0x001E293B : 0x00334155;
            uint32_t borderColor = cur->isFocused ? 0x003B82F6 : 0x00475569;
            GFX_FillRect(taskX, barY + 4, 100, 24, tabColor);
            GFX_DrawRect(taskX, barY + 4, 100, 24, borderColor);

            // Truncate title for taskbar tab
            char tabTitle[11];
            int i = 0;
            for (; i < 10 && cur->title[i]; i++) tabTitle[i] = cur->title[i];
            tabTitle[i] = '\0';

            GFX_DrawString(taskX + 8, barY + 12, tabTitle, GFX_COLOR_WHITE, GFX_COLOR_TRANSPARENT);
            taskX += 106;
        }
        cur = cur->next;
    }

    // System Status Tray (Right side of taskbar)
    char trayStr[64];
    uint32_t freeMb = PMM_GetFreeMemory() / (1024 * 1024);
    uint32_t totalMb = PMM_GetTotalMemory() / (1024 * 1024);
    snprintf(trayStr, sizeof(trayStr), "RAM: %u/%u MB | MLFQ 100Hz", freeMb, totalMb);
    GFX_DrawString(g_GuiWidth - 250, barY + 12, trayStr, GFX_COLOR_CYAN, GFX_COLOR_TRANSPARENT);
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

    // 4. Client content
    int32_t clientX = win->x + 1;
    int32_t clientY = win->y + TITLEBAR_HEIGHT + 1;
    int32_t clientW = win->width - 2;
    int32_t clientH = win->height - TITLEBAR_HEIGHT - 2;

    if (win->phys_buffer)
    {
        // Blit client-drawn backbuffer directly into compositor frame
        GFX_SetClip(clientX, clientY, clientW, clientH);
        GFX_BlitBuffer(clientX, clientY, (const uint32_t*)win->phys_buffer, clientW, clientH);
        GFX_ResetClip();
    }
    else
    {
        GFX_FillRect(clientX, clientY, clientW, clientH, GFX_COLOR_DARK_BLUE);
        if (win->onDraw)
        {
            GFX_SetClip(clientX, clientY, clientW, clientH);
            win->onDraw(win);
            GFX_ResetClip();
        }
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
    GFX_DrawString(x, y, "Version : 1.0 (Phase 19 Window Server & Ecosystem)", GFX_COLOR_WHITE, GFX_COLOR_TRANSPARENT);
    y += 12;
    GFX_DrawString(x, y, "Display : Bochs VBE 1024x768 @ 32bpp (LFB)", GFX_COLOR_WHITE, GFX_COLOR_TRANSPARENT);
    y += 12;
    GFX_DrawString(x, y, "Kernel  : 32-bit x86 Protected Mode + Paging", GFX_COLOR_WHITE, GFX_COLOR_TRANSPARENT);
    y += 12;
    GFX_DrawString(x, y, "Window  : Client-Server Shared Memory Compositor", GFX_COLOR_LIGHT_SLATE, GFX_COLOR_TRANSPARENT);
    y += 16;
    GFX_DrawString(x, y, "Click and drag any window title bar to move!", GFX_COLOR_AMBER, GFX_COLOR_TRANSPARENT);
}

// Window Content: File Explorer
static VFS_DirEntry s_ExplorerCache[32];
static int          s_ExplorerCount = -1;

static void CacheExplorerEntries(void)
{
    s_ExplorerCount = 0;
    VFS_DirEntry entry;
    size_t index = 0;
    // Read root directory entries
    while (vfs_readdir("/", index, &entry) == 0 && s_ExplorerCount < 16)
    {
        s_ExplorerCache[s_ExplorerCount++] = entry;
        index++;
    }
    // Read /bin directory entries
    index = 0;
    while (vfs_readdir("/bin", index, &entry) == 0 && s_ExplorerCount < 32)
    {
        char tmp[64];
        snprintf(tmp, sizeof(tmp), "bin/%s", entry.name);
        strncpy(entry.name, tmp, sizeof(entry.name) - 1);
        entry.name[sizeof(entry.name) - 1] = '\0';
        s_ExplorerCache[s_ExplorerCount++] = entry;
        index++;
    }
}

static void Draw_FileExplorerWindow(GUI_Window* win)
{
    int32_t x = win->x + 12;
    int32_t y = win->y + TITLEBAR_HEIGHT + 10;

    GFX_DrawString(x, y, "VFS File Explorer: /", GFX_COLOR_CYAN, GFX_COLOR_TRANSPARENT);

    // Search / Filter Box in File Explorer header
    int32_t fsBoxX = win->x + win->width - 190;
    int32_t fsBoxY = win->y + TITLEBAR_HEIGHT + 6;
    int32_t fsBoxW = 176;
    int32_t fsBoxH = 20;

    GFX_FillRect(fsBoxX, fsBoxY, fsBoxW, fsBoxH, 0x000F172A);
    GFX_DrawRect(fsBoxX, fsBoxY, fsBoxW, fsBoxH, g_FileExplorerSearchActive ? 0x0006B6D4 : 0x00334155);
    GFX_DrawString(fsBoxX + 6, fsBoxY + 5, "[?]", GFX_COLOR_CYAN, GFX_COLOR_TRANSPARENT);

    if (s_FileFilter[0] == '\0')
    {
        GFX_DrawString(fsBoxX + 28, fsBoxY + 5, "Search files...", GFX_COLOR_LIGHT_SLATE, GFX_COLOR_TRANSPARENT);
    }
    else
    {
        char filterDisp[36];
        snprintf(filterDisp, sizeof(filterDisp), "%s%s", s_FileFilter, g_FileExplorerSearchActive ? "_" : "");
        GFX_DrawString(fsBoxX + 28, fsBoxY + 5, filterDisp, GFX_COLOR_WHITE, GFX_COLOR_TRANSPARENT);
    }

    y += 18;
    GFX_DrawLine(x, y, x + win->width - 24, y, 0x00334155);
    y += 8;

    GFX_DrawString(x, y, "TYPE", GFX_COLOR_LIGHT_SLATE, GFX_COLOR_TRANSPARENT);
    GFX_DrawString(x + 55, y, "NAME", GFX_COLOR_LIGHT_SLATE, GFX_COLOR_TRANSPARENT);
    GFX_DrawString(x + 220, y, "SIZE", GFX_COLOR_LIGHT_SLATE, GFX_COLOR_TRANSPARENT);
    y += 14;

    GFX_DrawLine(x, y, x + win->width - 24, y, 0x001E293B);
    y += 8;

    if (s_ExplorerCount < 0)
    {
        CacheExplorerEntries();
    }

    int displayed = 0;
    for (int i = 0; i < s_ExplorerCount && displayed < 7; i++)
    {
        VFS_DirEntry* entry = &s_ExplorerCache[i];
        if (s_FileFilter[0] != '\0' && !str_contains_case_insensitive(entry->name, s_FileFilter))
        {
            continue;
        }

        if (entry->is_directory)
        {
            GFX_FillRect(x, y - 1, 42, 12, 0x00854D0E);
            GFX_DrawString(x + 4, y, "[DIR]", GFX_COLOR_AMBER, GFX_COLOR_TRANSPARENT);
            GFX_DrawString(x + 55, y, entry->name, GFX_COLOR_WHITE, GFX_COLOR_TRANSPARENT);
            GFX_DrawString(x + 220, y, "<DIR>", GFX_COLOR_LIGHT_SLATE, GFX_COLOR_TRANSPARENT);
        }
        else
        {
            GFX_FillRect(x, y - 1, 42, 12, 0x00065F46);
            GFX_DrawString(x + 4, y, "[FILE]", GFX_COLOR_EMERALD, GFX_COLOR_TRANSPARENT);
            GFX_DrawString(x + 55, y, entry->name, GFX_COLOR_WHITE, GFX_COLOR_TRANSPARENT);

            char sizeBuf[32];
            snprintf(sizeBuf, sizeof(sizeBuf), "%u bytes", entry->size);
            GFX_DrawString(x + 220, y, sizeBuf, GFX_COLOR_CYAN, GFX_COLOR_TRANSPARENT);
        }

        y += 16;
        displayed++;
    }

    if (displayed == 0)
    {
        GFX_DrawString(x + 10, y + 10, "No files matching search filter.", GFX_COLOR_AMBER, GFX_COLOR_TRANSPARENT);
    }

    // Bottom status bar in explorer
    int32_t statusY = win->y + win->height - 24;
    GFX_FillRect(win->x + 2, statusY, win->width - 4, 22, 0x00111827);
    GFX_DrawLine(win->x + 2, statusY, win->x + win->width - 2, statusY, 0x00334155);

    const ATA_DeviceInfo* dev = ATA_GetDevice();
    char devInfo[80];
    if (s_FileFilter[0] != '\0')
    {
        snprintf(devInfo, sizeof(devInfo), "Storage: ATA (%u MB) | %d file(s) matched \"%s\"",
                 dev ? dev->total_mb : 0, displayed, s_FileFilter);
    }
    else if (dev && dev->present)
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
    uint8_t flags = spinlock_acquire_irqsave(&g_GuiLock);
    if (!g_GuiDirty)
    {
        spinlock_release_irqrestore(&g_GuiLock, flags);
        return;
    }

    bool redrawDesktop = g_DesktopNeedsRedraw || g_StartMenuOpen;
    g_DesktopNeedsRedraw = false;
    int32_t minX = g_DirtyMinX;
    int32_t minY = g_DirtyMinY;
    int32_t maxX = g_DirtyMaxX;
    int32_t maxY = g_DirtyMaxY;

    // Reset dirty tracking atomically
    g_GuiDirty = false;
    g_DirtyMinX = g_GuiWidth;
    g_DirtyMinY = g_GuiHeight;
    g_DirtyMaxX = 0;
    g_DirtyMaxY = 0;
    spinlock_release_irqrestore(&g_GuiLock, flags);

    if (redrawDesktop)
    {
        // 1. Draw desktop background & taskbar
        GUI_DrawDesktop();

        // 2. Draw windows in reverse Z-order (bottom to top)
        GUI_Window* wins[32];
        int count = 0;
        GUI_Window* cur = g_WindowList;
        while (cur && count < 32)
        {
            wins[count++] = cur;
            cur = cur->next;
        }

        // Render from back (wins[count - 1]) to front (wins[0])
        for (int i = count - 1; i >= 0; i--)
        {
            GUI_DrawWindow(wins[i]);
            wins[i]->is_damaged = false;
        }

        // 3. Draw Start Menu & App Search Launcher (on top of windows)
        if (g_StartMenuOpen)
        {
            GUI_DrawStartMenu();
        }

        // Mouse pointer
        MouseState mouse;
        Mouse_GetState(&mouse);
        GFX_DrawCursor(mouse.x, mouse.y);

        // Blit full desktop to front Linear Framebuffer
        GFX_SwapBuffersDirty(0, 0, g_GuiWidth, g_GuiHeight);
    }
    else
    {
        // Fast path: Only composite damaged client windows directly without clearing
        GUI_Window* cur = g_WindowList;
        while (cur)
        {
            if (cur->isVisible && cur->is_damaged && cur->phys_buffer)
            {
                int32_t clientX = cur->x + 1;
                int32_t clientY = cur->y + TITLEBAR_HEIGHT + 1;
                int32_t clientW = cur->width - 2;
                int32_t clientH = cur->height - TITLEBAR_HEIGHT - 2;

                GFX_SetClip(clientX, clientY, clientW, clientH);
                GFX_BlitBuffer(clientX, clientY, (const uint32_t*)cur->phys_buffer, clientW, clientH);
                GFX_ResetClip();

                cur->is_damaged = false;
            }
            cur = cur->next;
        }

        // Mouse pointer
        MouseState mouse;
        Mouse_GetState(&mouse);
        GFX_DrawCursor(mouse.x, mouse.y);

        // Blit strictly damaged client bounding box
        int32_t dw = maxX - minX;
        int32_t dh = maxY - minY;
        if (dw > 0 && dh > 0)
        {
            GFX_SwapBuffersDirty(minX, minY, dw, dh);
        }
    }
}

void GUI_Task(void)
{
    log_info(MODULE, "GUI Compositor task started in MLFQ Queue 0.");

    while (g_GuiRunning)
    {
        // 1. Drain keyboard events
        while (i686_Keyboard_HasEvent())
        {
            KeyboardEvent kev;
            if (!i686_Keyboard_GetEvent(&kev)) break;

            // Global search hotkey: '/' opens app launcher if not in file search
            if (kev.pressed && kev.ascii == '/' && !g_StartMenuOpen && !g_FileExplorerSearchActive)
            {
                g_StartMenuOpen = true;
                g_AppSearchQuery[0] = '\0';
                g_AppSearchSelected = 0;
                g_DesktopNeedsRedraw = true;
                MarkRegionDirty(0, 0, g_GuiWidth, g_GuiHeight);
                continue;
            }

            if (g_StartMenuOpen)
            {
                if (kev.pressed)
                {
                    GUI_HandleStartMenuKey(&kev);
                }
            }
            else if (g_FileExplorerSearchActive)
            {
                if (kev.pressed)
                {
                    GUI_HandleFileExplorerKey(&kev);
                }
            }
            else if (g_WindowList && g_WindowList->isVisible && g_WindowList->owner_pid > 0)
            {
                gui_event_t ev;
                memset(&ev, 0, sizeof(ev));
                ev.type = kev.pressed ? GUI_EVENT_KEY_DOWN : GUI_EVENT_KEY_UP;
                ev.window_id = g_WindowList->id;
                ev.key_char = (uint32_t)kev.ascii;
                ev.key_code = (uint32_t)kev.scancode;
                ev.modifiers = (kev.shift ? 1 : 0) | (kev.ctrl ? 2 : 0) | (kev.alt ? 4 : 0);
                GUI_EnqueueEvent(g_WindowList, &ev);
            }
        }

        // 2. Poll mouse interaction
        MouseState mouse;
        Mouse_GetState(&mouse);

        bool leftClickDown = mouse.leftButton && !g_PrevLeftButton;
        bool leftClickUp   = !mouse.leftButton && g_PrevLeftButton;
        bool mouseMoved    = (mouse.x != g_PrevMouseX || mouse.y != g_PrevMouseY);
        g_PrevLeftButton   = mouse.leftButton;

        if (mouseMoved)
        {
            MarkRegionDirty(g_PrevMouseX - 2, g_PrevMouseY - 2, 22, 28);
            MarkRegionDirty(mouse.x - 2, mouse.y - 2, 22, 28);
            g_DesktopNeedsRedraw = true;
        }

        int32_t barY = g_GuiHeight - TASKBAR_HEIGHT;

        if (leftClickDown)
        {
            bool handled = false;

            // A. Check Taskbar Click
            if (mouse.y >= barY && mouse.y < (int32_t)g_GuiHeight)
            {
                // Start Button Click: 6 to 82
                if (mouse.x >= 6 && mouse.x <= 82)
                {
                    g_StartMenuOpen = !g_StartMenuOpen;
                    g_DesktopNeedsRedraw = true;
                    MarkRegionDirty(0, 0, g_GuiWidth, g_GuiHeight);
                    handled = true;
                }
                // Search Bar Click: 88 to 288
                else if (mouse.x >= 88 && mouse.x <= 288)
                {
                    g_StartMenuOpen = true;
                    g_DesktopNeedsRedraw = true;
                    MarkRegionDirty(0, 0, g_GuiWidth, g_GuiHeight);
                    handled = true;
                }
                // Running Task Tabs Click
                else if (mouse.x >= 296)
                {
                    int32_t curTabX = 296;
                    GUI_Window* curW = g_WindowList;
                    while (curW)
                    {
                        if (curW->isVisible)
                        {
                            if (mouse.x >= curTabX && mouse.x < curTabX + 100)
                            {
                                GUI_FocusWindow(curW);
                                g_DesktopNeedsRedraw = true;
                                MarkRegionDirty(0, 0, g_GuiWidth, g_GuiHeight);
                                handled = true;
                                break;
                            }
                            curTabX += 106;
                        }
                        curW = curW->next;
                    }
                }
            }

            // B. Check Start Menu Popup Click
            if (!handled && g_StartMenuOpen)
            {
                int32_t menuX = 6;
                int32_t menuW = 390;
                int32_t menuH = 370;
                int32_t menuY = barY - menuH - 4;

                if (mouse.x >= menuX && mouse.x < menuX + menuW &&
                    mouse.y >= menuY && mouse.y < menuY + menuH)
                {
                    // Check if clicked an item row
                    int matches[16];
                    int matchCount = GetMatchingCatalogIndices(matches, 16);
                    for (int idx = 0; idx < matchCount && idx < 6; idx++)
                    {
                        int32_t rowY = menuY + 86 + (idx * 41);
                        if (mouse.y >= rowY && mouse.y < rowY + 38)
                        {
                            GUI_LaunchCatalogItem(matches[idx]);
                            handled = true;
                            break;
                        }
                    }
                    handled = true; // Click inside menu doesn't pass through
                }
                else
                {
                    // Click outside Start Menu closes it
                    g_StartMenuOpen = false;
                    g_DesktopNeedsRedraw = true;
                    MarkRegionDirty(0, 0, g_GuiWidth, g_GuiHeight);
                }
            }

            // C. Check File Explorer Search Box Click
            if (!handled)
            {
                GUI_Window* wExp = GUI_FindWindowByTitle("File Explorer (/)");
                if (wExp && wExp->isVisible)
                {
                    int32_t fsBoxX = wExp->x + wExp->width - 190;
                    int32_t fsBoxY = wExp->y + TITLEBAR_HEIGHT + 6;
                    int32_t fsBoxW = 176;
                    int32_t fsBoxH = 20;

                    if (mouse.x >= fsBoxX && mouse.x <= fsBoxX + fsBoxW &&
                        mouse.y >= fsBoxY && mouse.y <= fsBoxY + fsBoxH)
                    {
                        g_FileExplorerSearchActive = true;
                        GUI_FocusWindow(wExp);
                        g_DesktopNeedsRedraw = true;
                        MarkRegionDirty(0, 0, g_GuiWidth, g_GuiHeight);
                        handled = true;
                    }
                    else
                    {
                        if (g_FileExplorerSearchActive)
                        {
                            g_FileExplorerSearchActive = false;
                            g_DesktopNeedsRedraw = true;
                            MarkRegionDirty(0, 0, g_GuiWidth, g_GuiHeight);
                        }
                    }
                }
            }

            // D. Window Interaction (Drag, Close, Client Click)
            if (!handled)
            {
                GUI_Window* wins[32];
                int count = 0;
                GUI_Window* cur = g_WindowList;
                while (cur && count < 32)
                {
                    wins[count++] = cur;
                    cur = cur->next;
                }

                for (int i = 0; i < count; i++)
                {
                    GUI_Window* w = wins[i];
                    if (!w->isVisible) continue;

                    // Check close button
                    int32_t closeX = w->x + w->width - 19;
                    int32_t closeY = w->y + 4;
                    if (mouse.x >= closeX && mouse.x <= closeX + 15 &&
                        mouse.y >= closeY && mouse.y <= closeY + 15)
                    {
                        if (w->owner_pid > 0)
                        {
                            gui_event_t ev;
                            memset(&ev, 0, sizeof(ev));
                            ev.type = GUI_EVENT_CLOSE;
                            ev.window_id = w->id;
                            GUI_EnqueueEvent(w, &ev);
                        }
                        w->isVisible = false;
                        g_DesktopNeedsRedraw = true;
                        MarkRegionDirty(0, 0, g_GuiWidth, g_GuiHeight);
                        break;
                    }

                    // Check titlebar drag
                    if (mouse.x >= w->x && mouse.x <= w->x + w->width &&
                        mouse.y >= w->y && mouse.y <= w->y + TITLEBAR_HEIGHT)
                    {
                        GUI_FocusWindow(w);
                        w->isDragging = true;
                        w->dragOffsetX = mouse.x - w->x;
                        w->dragOffsetY = mouse.y - w->y;
                        g_ActiveDragWindow = w;
                        g_DesktopNeedsRedraw = true;
                        MarkRegionDirty(0, 0, g_GuiWidth, g_GuiHeight);
                        break;
                    }

                    // Check client area click
                    int32_t clientX = w->x + 1;
                    int32_t clientY = w->y + TITLEBAR_HEIGHT + 1;
                    int32_t clientW = w->width - 2;
                    int32_t clientH = w->height - TITLEBAR_HEIGHT - 2;

                    if (mouse.x >= clientX && mouse.x < clientX + clientW &&
                        mouse.y >= clientY && mouse.y < clientY + clientH)
                    {
                        GUI_FocusWindow(w);
                        if (w->owner_pid > 0)
                        {
                            gui_event_t ev;
                            memset(&ev, 0, sizeof(ev));
                            ev.type = GUI_EVENT_MOUSE_DOWN;
                            ev.window_id = w->id;
                            ev.mouse_x = mouse.x - clientX;
                            ev.mouse_y = mouse.y - clientY;
                            ev.button = 1;
                            GUI_EnqueueEvent(w, &ev);
                        }
                        break;
                    }

                    // Check general frame
                    if (mouse.x >= w->x && mouse.x <= w->x + w->width &&
                        mouse.y >= w->y && mouse.y <= w->y + w->height)
                    {
                        GUI_FocusWindow(w);
                        g_DesktopNeedsRedraw = true;
                        MarkRegionDirty(0, 0, g_GuiWidth, g_GuiHeight);
                        break;
                    }
                }
            }
        }
        else if (leftClickUp)
        {
            if (g_WindowList && g_WindowList->isVisible && g_WindowList->owner_pid > 0)
            {
                GUI_Window* w = g_WindowList;
                int32_t clientX = w->x + 1;
                int32_t clientY = w->y + TITLEBAR_HEIGHT + 1;
                gui_event_t ev;
                memset(&ev, 0, sizeof(ev));
                ev.type = GUI_EVENT_MOUSE_UP;
                ev.window_id = w->id;
                ev.mouse_x = mouse.x - clientX;
                ev.mouse_y = mouse.y - clientY;
                ev.button = 1;
                GUI_EnqueueEvent(w, &ev);
            }
        }
        else if (mouseMoved)
        {
            if (g_WindowList && g_WindowList->isVisible && g_WindowList->owner_pid > 0)
            {
                GUI_Window* w = g_WindowList;
                int32_t clientX = w->x + 1;
                int32_t clientY = w->y + TITLEBAR_HEIGHT + 1;
                int32_t clientW = w->width - 2;
                int32_t clientH = w->height - TITLEBAR_HEIGHT - 2;

                if (mouse.x >= clientX && mouse.x < clientX + clientW &&
                    mouse.y >= clientY && mouse.y < clientY + clientH)
                {
                    gui_event_t ev;
                    memset(&ev, 0, sizeof(ev));
                    ev.type = GUI_EVENT_MOUSE_MOVE;
                    ev.window_id = w->id;
                    ev.mouse_x = mouse.x - clientX;
                    ev.mouse_y = mouse.y - clientY;
                    ev.button = mouse.leftButton ? 1 : 0;
                    GUI_EnqueueEvent(w, &ev);
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
                g_DesktopNeedsRedraw = true;
                MarkRegionDirty(0, 0, g_GuiWidth, g_GuiHeight);
            }
            else
            {
                g_ActiveDragWindow->isDragging = false;
                g_ActiveDragWindow = NULL;
                g_DesktopNeedsRedraw = true;
                MarkRegionDirty(0, 0, g_GuiWidth, g_GuiHeight);
            }
        }

        g_PrevMouseX = mouse.x;
        g_PrevMouseY = mouse.y;

        // Check if any client window is damaged
        GUI_Window* w = g_WindowList;
        while (w)
        {
            if (w->isVisible && w->is_damaged)
            {
                g_GuiDirty = true;
                break;
            }
            w = w->next;
        }

        if (g_GuiDirty)
        {
            GUI_Render();
        }

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
