#ifndef STM32U083_PLATFORM_SUPERVISION_H
#define STM32U083_PLATFORM_SUPERVISION_H

#include <stdbool.h>
#include <stdint.h>

#include "stm32u083_platform/PlatformBoard.h"

typedef enum {
    PLATFORM_RESET_UNKNOWN = 0,
    PLATFORM_RESET_POWER_ON = 1,
    PLATFORM_RESET_PIN = 2,
    PLATFORM_RESET_SOFTWARE = 3,
    PLATFORM_RESET_WATCHDOG = 4,
    PLATFORM_RESET_BROWNOUT = 5
} Platform_ResetReason;

typedef struct {
    Platform_ResetReason reason;
    uint16_t raw_flags;
    uint32_t boot_count;
} Platform_ResetInfo;

/* Call once at the very start of main(), before HAL initialization clears context. */
void PlatformSupervision_CaptureReset(void);
Platform_ResetInfo PlatformSupervision_GetResetInfo(void);

/* Starts the independent watchdog. It cannot be stopped after this call. */
Platform_Status PlatformSupervision_StartWatchdog(void);
void PlatformSupervision_RefreshWatchdog(void);

#endif
