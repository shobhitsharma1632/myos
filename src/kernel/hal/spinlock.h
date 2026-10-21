#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    volatile uint32_t lock;
    uint32_t          owner_core;
    uint32_t          recurse_depth;
} Spinlock;

#define SPINLOCK_INIT { 0, 0xFFFFFFFF, 0 }

void    spinlock_init(Spinlock* lock);
void    spinlock_acquire(Spinlock* lock);
void    spinlock_release(Spinlock* lock);
bool    spinlock_try_acquire(Spinlock* lock);
uint8_t spinlock_acquire_irqsave(Spinlock* lock);
void    spinlock_release_irqrestore(Spinlock* lock, uint8_t irq_state);
