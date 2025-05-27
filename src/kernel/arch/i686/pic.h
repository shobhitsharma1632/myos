#pragma once

#include <stdint.h>

void i686_PIC_Configure(uint8_t offserPic1, uint8_t offsetPic2);
void i686_PIC_SendEndOfInterrupt(uint8_t irq);
void i686_PIC_Disable();
void i686_PIC_Mask(int irq);
void i686_PIC_Unmask(int irq);
uint16_t i686_PIC_ReadIrqRequestRegister();
uint16_t i686_PIC_ReadInServiceRegister();
