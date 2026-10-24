#include <stdio.h>
#include <arch/i686/io.h>

#include <stdarg.h>
#include <stdbool.h>

const unsigned SCREEN_WIDTH = 80;
const unsigned SCREEN_HEIGHT = 25;
const uint8_t DEFAULT_COLOR = 0x7;

uint8_t* g_ScreenBuffer = (uint8_t*)0xB8000;
int g_ScreenX = 0, g_ScreenY = 0;

void VGA_putchr(int x, int y, char c)
{
    g_ScreenBuffer[2 * (y * SCREEN_WIDTH + x)] = c;
}

void VGA_putcolor(int x, int y, uint8_t color)
{
    g_ScreenBuffer[2 * (y * SCREEN_WIDTH + x) + 1] = color;
}

char VGA_getchr(int x, int y)
{
    return g_ScreenBuffer[2 * (y * SCREEN_WIDTH + x)];
}

uint8_t VGA_getcolor(int x, int y)
{
    return g_ScreenBuffer[2 * (y * SCREEN_WIDTH + x) + 1];
}

void VGA_setcursor(int x, int y)
{
    int pos = y * SCREEN_WIDTH + x;

    i686_outb(0x3D4, 0x0F);
    i686_outb(0x3D5, (uint8_t)(pos & 0xFF));
    i686_outb(0x3D4, 0x0E);
    i686_outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

void VGA_clrscr()
{
    for (int y = 0; y < SCREEN_HEIGHT; y++)
        for (int x = 0; x < SCREEN_WIDTH; x++)
        {
            VGA_putchr(x, y, '\0');
            VGA_putcolor(x, y, DEFAULT_COLOR);
        }

    g_ScreenX = 0;
    g_ScreenY = 0;
    VGA_setcursor(g_ScreenX, g_ScreenY);
}

void VGA_scrollback(int lines)
{
    for (int y = lines; y < SCREEN_HEIGHT; y++)
        for (int x = 0; x < SCREEN_WIDTH; x++)
        {
            VGA_putchr(x, y - lines, VGA_getchr(x, y));
            VGA_putcolor(x, y - lines, VGA_getcolor(x, y));
        }

    for (int y = SCREEN_HEIGHT - lines; y < SCREEN_HEIGHT; y++)
        for (int x = 0; x < SCREEN_WIDTH; x++)
        {
            VGA_putchr(x, y, '\0');
            VGA_putcolor(x, y, DEFAULT_COLOR);
        }

    g_ScreenY -= lines;
}

typedef enum {
    ANSI_STATE_NORMAL,
    ANSI_STATE_ESC,
    ANSI_STATE_CSI
} AnsiState;

static AnsiState g_AnsiState = ANSI_STATE_NORMAL;
static char g_AnsiBuf[32];
static int g_AnsiLen = 0;

void VGA_putc(char c)
{
    if (g_AnsiState == ANSI_STATE_NORMAL)
    {
        if (c == '\033')
        {
            g_AnsiState = ANSI_STATE_ESC;
            return;
        }
    }
    else if (g_AnsiState == ANSI_STATE_ESC)
    {
        if (c == '[')
        {
            g_AnsiState = ANSI_STATE_CSI;
            g_AnsiLen = 0;
            g_AnsiBuf[0] = '\0';
            return;
        }
        else
        {
            g_AnsiState = ANSI_STATE_NORMAL;
        }
    }
    else if (g_AnsiState == ANSI_STATE_CSI)
    {
        if ((c >= '0' && c <= '9') || c == ';')
        {
            if (g_AnsiLen < (int)sizeof(g_AnsiBuf) - 1)
            {
                g_AnsiBuf[g_AnsiLen++] = c;
                g_AnsiBuf[g_AnsiLen] = '\0';
            }
            return;
        }
        else if (c == 'H' || c == 'f')
        {
            // Cursor position
            if (g_AnsiLen == 0)
            {
                g_ScreenX = 0;
                g_ScreenY = 0;
            }
            else
            {
                int row = 0, col = 0;
                int i = 0;
                while (g_AnsiBuf[i] >= '0' && g_AnsiBuf[i] <= '9') {
                    row = row * 10 + (g_AnsiBuf[i] - '0');
                    i++;
                }
                if (g_AnsiBuf[i] == ';') {
                    i++;
                    while (g_AnsiBuf[i] >= '0' && g_AnsiBuf[i] <= '9') {
                        col = col * 10 + (g_AnsiBuf[i] - '0');
                        i++;
                    }
                }
                g_ScreenY = (row > 0 && row <= (int)SCREEN_HEIGHT) ? (row - 1) : 0;
                g_ScreenX = (col > 0 && col <= (int)SCREEN_WIDTH) ? (col - 1) : 0;
            }
            VGA_setcursor(g_ScreenX, g_ScreenY);
            g_AnsiState = ANSI_STATE_NORMAL;
            return;
        }
        else if (c == 'J')
        {
            // \033[2J = clear screen
            VGA_clrscr();
            g_AnsiState = ANSI_STATE_NORMAL;
            return;
        }
        else
        {
            // Ignore/consume any other control code (e.g. 'm')
            g_AnsiState = ANSI_STATE_NORMAL;
            return;
        }
    }

    switch (c)
    {
        case '\n':
            g_ScreenX = 0;
            g_ScreenY++;
            break;
    
        case '\t':
            for (int i = 0; i < 4 - (g_ScreenX % 4); i++)
                VGA_putc(' ');
            break;

        case '\r':
            g_ScreenX = 0;
            break;

        case '\b':
            if (g_ScreenX > 0)
            {
                g_ScreenX--;
            }
            else if (g_ScreenY > 0)
            {
                g_ScreenY--;
                g_ScreenX = SCREEN_WIDTH - 1;
            }
            break;

        default:
            VGA_putchr(g_ScreenX, g_ScreenY, c);
            g_ScreenX++;
            break;
    }

    if (g_ScreenX >= SCREEN_WIDTH)
    {
        g_ScreenY++;
        g_ScreenX = 0;
    }
    if (g_ScreenY >= SCREEN_HEIGHT)
        VGA_scrollback(1);

    VGA_setcursor(g_ScreenX, g_ScreenY);
}
