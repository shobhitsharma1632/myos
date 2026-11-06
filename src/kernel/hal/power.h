#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    POWER_ACTION_NONE = 0,
    POWER_ACTION_SHUTDOWN,
    POWER_ACTION_REBOOT,
    POWER_ACTION_SLEEP,
    POWER_ACTION_HIBERNATE
} PowerAction;

void Power_Initialize(void);
void Power_Shutdown(void);
void Power_Reboot(void);
void Power_Sleep(void);
void Power_Hibernate(void);
void Power_Wakeup(void);
bool Power_IsSleeping(void);
bool Power_IsHibernating(void);
