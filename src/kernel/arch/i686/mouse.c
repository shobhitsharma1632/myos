#include "mouse.h"
#include <arch/i686/io.h>
#include <arch/i686/irq.h>
#include <debug.h>
#include <stdbool.h>

#define MODULE "Mouse"

#define MOUSE_DATA_PORT     0x60
#define MOUSE_STATUS_PORT   0x64
#define MOUSE_COMMAND_PORT  0x64

#define MOUSE_TIMEOUT       100000

static uint32_t g_ScreenWidth = 1024;
static uint32_t g_ScreenHeight = 768;

static volatile int32_t g_MouseX = 512;
static volatile int32_t g_MouseY = 384;
static volatile bool    g_LeftButton = false;
static volatile bool    g_RightButton = false;
static volatile bool    g_MiddleButton = false;

static uint8_t g_MouseCycle = 0;
static uint8_t g_MousePacket[3];

static void Mouse_WaitWrite(void)
{
    uint32_t timeout = MOUSE_TIMEOUT;
    while (timeout--)
    {
        if ((i686_inb(MOUSE_STATUS_PORT) & 2) == 0)
            return;
    }
}

static void Mouse_WaitRead(void)
{
    uint32_t timeout = MOUSE_TIMEOUT;
    while (timeout--)
    {
        if ((i686_inb(MOUSE_STATUS_PORT) & 1) == 1)
            return;
    }
}

static void Mouse_Write(uint8_t writeVal)
{
    // Tell 8042 controller next byte is directed to auxiliary device
    Mouse_WaitWrite();
    i686_outb(MOUSE_COMMAND_PORT, 0xD4);
    Mouse_WaitWrite();
    i686_outb(MOUSE_DATA_PORT, writeVal);
}

static uint8_t Mouse_Read(void)
{
    Mouse_WaitRead();
    return i686_inb(MOUSE_DATA_PORT);
}

static void i686_Mouse_Handler(Registers* regs)
{
    (void)regs;

    uint8_t status = i686_inb(MOUSE_STATUS_PORT);
    if ((status & 1) == 0)
        return;

    // Check if data is from mouse (bit 5)
    if ((status & 0x20) == 0)
        return;

    uint8_t data = i686_inb(MOUSE_DATA_PORT);

    switch (g_MouseCycle)
    {
        case 0:
            // Bit 3 must always be 1 for valid PS/2 packet alignment
            if ((data & 0x08) != 0)
            {
                g_MousePacket[0] = data;
                g_MouseCycle = 1;
            }
            break;

        case 1:
            g_MousePacket[1] = data;
            g_MouseCycle = 2;
            break;

        case 2:
            g_MousePacket[2] = data;
            g_MouseCycle = 0;

            uint8_t flags = g_MousePacket[0];
            g_LeftButton = (flags & 1) != 0;
            g_RightButton = (flags & 2) != 0;
            g_MiddleButton = (flags & 4) != 0;

            int32_t dx = g_MousePacket[1];
            if (flags & 0x10) dx |= 0xFFFFFF00; // Sign extend X

            int32_t dy = g_MousePacket[2];
            if (flags & 0x20) dy |= 0xFFFFFF00; // Sign extend Y

            // PS/2 mouse +Y points up; screen coordinate +Y points down
            g_MouseX += dx;
            g_MouseY -= dy;

            // Clamp coordinates to screen bounds
            if (g_MouseX < 0) g_MouseX = 0;
            if (g_MouseX >= (int32_t)g_ScreenWidth) g_MouseX = g_ScreenWidth - 1;
            if (g_MouseY < 0) g_MouseY = 0;
            if (g_MouseY >= (int32_t)g_ScreenHeight) g_MouseY = g_ScreenHeight - 1;
            break;
    }
}

void i686_Mouse_Initialize(uint32_t screenWidth, uint32_t screenHeight)
{
    g_ScreenWidth = screenWidth;
    g_ScreenHeight = screenHeight;
    g_MouseX = screenWidth / 2;
    g_MouseY = screenHeight / 2;
    g_MouseCycle = 0;

    // 1. Enable auxiliary mouse device on 8042 controller
    Mouse_WaitWrite();
    i686_outb(MOUSE_COMMAND_PORT, 0xA8);

    // 2. Enable interrupts: read controller configuration byte
    Mouse_WaitWrite();
    i686_outb(MOUSE_COMMAND_PORT, 0x20);
    uint8_t status = Mouse_Read();

    // Set bit 1 (enable IRQ 12), clear bit 5 (enable mouse clock)
    status |= (1 << 1);
    status &= ~(1 << 5);

    // Write controller configuration byte back
    Mouse_WaitWrite();
    i686_outb(MOUSE_COMMAND_PORT, 0x60);
    Mouse_WaitWrite();
    i686_outb(MOUSE_DATA_PORT, status);

    // 3. Set default settings
    Mouse_Write(0xF6);
    Mouse_Read(); // Acknowledge 0xFA

    // 4. Enable packet streaming
    Mouse_Write(0xF4);
    Mouse_Read(); // Acknowledge 0xFA

    // 5. Register IRQ 12 handler (automatically unmasks IRQ 12 on Slave PIC and IRQ 2 on Master PIC)
    i686_IRQ_RegisterHandler(12, i686_Mouse_Handler);

    log_info(MODULE, "PS/2 Mouse driver initialized (IRQ 12 registered). Screen bounds: %ux%u",
             screenWidth, screenHeight);
}

void Mouse_SetScreenBounds(uint32_t width, uint32_t height)
{
    g_ScreenWidth = width;
    g_ScreenHeight = height;
    if (g_MouseX >= (int32_t)width) g_MouseX = width - 1;
    if (g_MouseY >= (int32_t)height) g_MouseY = height - 1;
}

void Mouse_GetState(MouseState* state)
{
    if (!state) return;
    state->x = g_MouseX;
    state->y = g_MouseY;
    state->leftButton = g_LeftButton;
    state->rightButton = g_RightButton;
    state->middleButton = g_MiddleButton;
}

void Mouse_SetPosition(int32_t x, int32_t y)
{
    if (x < 0) x = 0;
    if (x >= (int32_t)g_ScreenWidth) x = g_ScreenWidth - 1;
    if (y < 0) y = 0;
    if (y >= (int32_t)g_ScreenHeight) y = g_ScreenHeight - 1;
    g_MouseX = x;
    g_MouseY = y;
}

void Mouse_InjectEvent(int32_t deltaX, int32_t deltaY, bool left, bool right, bool middle)
{
    g_MouseX += deltaX;
    g_MouseY += deltaY;
    if (g_MouseX < 0) g_MouseX = 0;
    if (g_MouseX >= (int32_t)g_ScreenWidth) g_MouseX = g_ScreenWidth - 1;
    if (g_MouseY < 0) g_MouseY = 0;
    if (g_MouseY >= (int32_t)g_ScreenHeight) g_MouseY = g_ScreenHeight - 1;

    g_LeftButton = left;
    g_RightButton = right;
    g_MiddleButton = middle;
}
