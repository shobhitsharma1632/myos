#pragma once

#include <stdint.h>
#include <stdbool.h>

void i686_Keyboard_Initialize(void);
bool i686_Keyboard_HasChar(void);
char i686_Keyboard_GetChar(void);
char i686_Keyboard_WaitChar(void);

bool i686_Keyboard_IsShiftPressed(void);
bool i686_Keyboard_IsCtrlPressed(void);
bool i686_Keyboard_IsCapsLocked(void);

void i686_Keyboard_InjectScancode(uint8_t scancode);
void i686_Keyboard_InjectChar(char c);
