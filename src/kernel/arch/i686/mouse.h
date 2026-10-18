#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int32_t x;
    int32_t y;
    bool leftButton;
    bool rightButton;
    bool middleButton;
} MouseState;

void i686_Mouse_Initialize(uint32_t screenWidth, uint32_t screenHeight);
void Mouse_SetScreenBounds(uint32_t width, uint32_t height);
void Mouse_GetState(MouseState* state);
void Mouse_SetPosition(int32_t x, int32_t y);
void Mouse_InjectEvent(int32_t deltaX, int32_t deltaY, bool left, bool right, bool middle);
