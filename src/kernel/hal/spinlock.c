#include "spinlock.h"
#include <arch/i686/io.h>

void spinlock_init(Spinlock* lock)
{
    if (!lock) return;
    lock->lock = 0;
    lock->owner_core = 0xFFFFFFFF;
    lock->recurse_depth = 0;
}

void spinlock_acquire(Spinlock* lock)
{
    if (!lock) return;

    while (__atomic_test_and_set(&lock->lock, __ATOMIC_ACQUIRE))
    {
        while (__atomic_load_n(&lock->lock, __ATOMIC_RELAXED))
        {
            __asm__ volatile("pause");
        }
    }
}

bool spinlock_try_acquire(Spinlock* lock)
{
    if (!lock) return false;
    return !__atomic_test_and_set(&lock->lock, __ATOMIC_ACQUIRE);
}

void spinlock_release(Spinlock* lock)
{
    if (!lock) return;
    lock->owner_core = 0xFFFFFFFF;
    lock->recurse_depth = 0;
    __atomic_clear(&lock->lock, __ATOMIC_RELEASE);
}

uint8_t spinlock_acquire_irqsave(Spinlock* lock)
{
    uint8_t irq_state = i686_DisableInterrupts();
    spinlock_acquire(lock);
    return irq_state;
}

void spinlock_release_irqrestore(Spinlock* lock, uint8_t irq_state)
{
    spinlock_release(lock);
    if (irq_state)
    {
        i686_EnableInterrupts();
    }
}
