#include "keyboard.h"
#include <arch/i686/io.h>
#include <arch/i686/irq.h>
#include <hal/task.h>
#include <debug.h>
#include <stdbool.h>

#define MODULE "Keyboard"

#define KEYBOARD_DATA_PORT      0x60
#define KEYBOARD_STATUS_PORT    0x64
#define KEYBOARD_BUFFER_SIZE    256

#define SCANCODE_EXTENDED       0xE0
#define SCANCODE_LSHIFT         0x2A
#define SCANCODE_RSHIFT         0x36
#define SCANCODE_LCTRL          0x1D
#define SCANCODE_LALT           0x38
#define SCANCODE_CAPSLOCK       0x3A

static const char g_ScancodeTable_Normal[128] = {
    0,    27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*',  0,   ' ',  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,    0,   0,   0,   0,   '-', 0,   0,   0,   '+', 0,   0,   0,
    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
};

static const char g_ScancodeTable_Shift[128] = {
    0,    27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*',  0,   ' ',  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,    0,   0,   0,   0,   '-', 0,   0,   0,   '+', 0,   0,   0,
    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
};

static volatile char     g_KeyBuffer[KEYBOARD_BUFFER_SIZE];
static volatile uint32_t g_KeyHead = 0;
static volatile uint32_t g_KeyTail = 0;

static bool g_LShift = false;
static bool g_RShift = false;
static bool g_LCtrl = false;
static bool g_LAlt = false;
static bool g_CapsLock = false;
static bool g_Extended = false;

static void Keyboard_PushChar(char c)
{
    if (c == 0) return;
    uint32_t next = (g_KeyHead + 1) % KEYBOARD_BUFFER_SIZE;
    if (next != g_KeyTail)
    {
        g_KeyBuffer[g_KeyHead] = c;
        g_KeyHead = next;
    }
}

static void i686_Keyboard_ProcessScancode(uint8_t scancode)
{
    if (scancode == SCANCODE_EXTENDED)
    {
        g_Extended = true;
        return;
    }

    bool isBreak = (scancode & 0x80) != 0;
    uint8_t code = scancode & 0x7F;

    if (g_Extended)
    {
        g_Extended = false;
        // Extended keys (arrows, right ctrl, etc.)
        if (code == SCANCODE_LCTRL)
            g_LCtrl = !isBreak;
        else if (code == SCANCODE_LALT)
            g_LAlt = !isBreak;
        return;
    }

    if (isBreak)
    {
        // Key release
        if (code == SCANCODE_LSHIFT)
            g_LShift = false;
        else if (code == SCANCODE_RSHIFT)
            g_RShift = false;
        else if (code == SCANCODE_LCTRL)
            g_LCtrl = false;
        else if (code == SCANCODE_LALT)
            g_LAlt = false;
    }
    else
    {
        // Key press (Make code)
        if (code == SCANCODE_LSHIFT)
            g_LShift = true;
        else if (code == SCANCODE_RSHIFT)
            g_RShift = true;
        else if (code == SCANCODE_LCTRL)
            g_LCtrl = true;
        else if (code == SCANCODE_LALT)
            g_LAlt = true;
        else if (code == SCANCODE_CAPSLOCK)
            g_CapsLock = !g_CapsLock;
        else if (code < 128)
        {
            bool shift = (g_LShift || g_RShift);
            char c = 0;

            if (g_CapsLock)
            {
                char norm = g_ScancodeTable_Normal[code];
                if (norm >= 'a' && norm <= 'z')
                {
                    // If Shift is also held, invert CapsLock
                    c = shift ? norm : (norm - 32);
                }
                else
                {
                    c = shift ? g_ScancodeTable_Shift[code] : norm;
                }
            }
            else
            {
                c = shift ? g_ScancodeTable_Shift[code] : g_ScancodeTable_Normal[code];
            }

            // Handle Ctrl+C or Ctrl+L combinations
            if (g_LCtrl && (c == 'c' || c == 'C'))
            {
                c = 0x03; // ETX / Interrupt signal
                Task* cur = Task_GetCurrent();
                if (cur && cur->is_user)
                {
                    Task_SendSignal(cur->id, SIGINT);
                }
            }
            else if (g_LCtrl && (c == 'l' || c == 'L'))
                c = 0x0C; // Form Feed / Clear screen

            if (c != 0)
                Keyboard_PushChar(c);
        }
    }
}

static void i686_Keyboard_Handler(Registers* regs)
{
    (void)regs;
    // Read raw scancode from PS/2 Data Port 0x60
    uint8_t scancode = i686_inb(KEYBOARD_DATA_PORT);
    i686_Keyboard_ProcessScancode(scancode);
}

void i686_Keyboard_Initialize(void)
{
    g_KeyHead = 0;
    g_KeyTail = 0;
    g_LShift = false;
    g_RShift = false;
    g_LCtrl = false;
    g_LAlt = false;
    g_CapsLock = false;
    g_Extended = false;

    // Flush any stale data from PS/2 output buffer
    while (i686_inb(KEYBOARD_STATUS_PORT) & 1)
    {
        i686_inb(KEYBOARD_DATA_PORT);
    }

    // Register IRQ 1 handler (automatically unmasks IRQ 1 on the 8259 PIC)
    i686_IRQ_RegisterHandler(1, i686_Keyboard_Handler);

    log_info(MODULE, "PS/2 Keyboard Driver initialized (IRQ 1 registered).");
}

bool i686_Keyboard_HasChar(void)
{
    return (g_KeyHead != g_KeyTail);
}

char i686_Keyboard_GetChar(void)
{
    if (g_KeyHead == g_KeyTail)
        return 0;

    char c = g_KeyBuffer[g_KeyTail];
    g_KeyTail = (g_KeyTail + 1) % KEYBOARD_BUFFER_SIZE;
    return c;
}

char i686_Keyboard_WaitChar(void)
{
    while (!i686_Keyboard_HasChar())
    {
        if (Task_IsActive())
            Task_Yield();
        else
            i686_Halt();
    }
    return i686_Keyboard_GetChar();
}

bool i686_Keyboard_IsShiftPressed(void)
{
    return (g_LShift || g_RShift);
}

bool i686_Keyboard_IsCtrlPressed(void)
{
    return g_LCtrl;
}

bool i686_Keyboard_IsCapsLocked(void)
{
    return g_CapsLock;
}

void i686_Keyboard_InjectScancode(uint8_t scancode)
{
    i686_Keyboard_ProcessScancode(scancode);
}

void i686_Keyboard_InjectChar(char c)
{
    Keyboard_PushChar(c);
}
