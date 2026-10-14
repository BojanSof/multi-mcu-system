#ifndef STM32U083_PLATFORM_PLATFORM_DEBUG_H
#define STM32U083_PLATFORM_PLATFORM_DEBUG_H

#include <stdbool.h>

#include "stm32u083_platform/PlatformBoard.h"

Platform_Status PlatformDebug_Initialize(void);
void PlatformDebug_Set(bool active);

#endif
