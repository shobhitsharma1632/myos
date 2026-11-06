#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t scancode;
    char    ascii;
    bool    pressed; // true = down (make), false = up (break)
    bool    shift;
    bool    ctrl;
    bool    alt;
} KeyboardEvent;

void i686_Keyboard_Initialize(void);
bool i686_Keyboard_HasChar(void);
char i686_Keyboard_GetChar(void);
char i686_Keyboard_WaitChar(void);

bool i686_Keyboard_HasEvent(void);
bool i686_Keyboard_GetEvent(KeyboardEvent* ev);

bool i686_Keyboard_IsShiftPressed(void);
bool i686_Keyboard_IsCtrlPressed(void);
bool i686_Keyboard_IsCapsLocked(void);
bool i686_Keyboard_IsKeyDown(uint8_t scancode);

void i686_Keyboard_InjectScancode(uint8_t scancode);
void i686_Keyboard_InjectChar(char c);

