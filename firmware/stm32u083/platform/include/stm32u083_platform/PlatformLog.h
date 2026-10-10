#ifndef STM32U083_PLATFORM_LOG_H
#define STM32U083_PLATFORM_LOG_H

#include <stddef.h>

#include "stm32u083_platform/PlatformBoard.h"

Platform_Status PlatformLog_Initialize(void);
Platform_Status PlatformLog_Write(const char *message, size_t length);

#endif
