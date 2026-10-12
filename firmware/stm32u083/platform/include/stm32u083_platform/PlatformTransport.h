#ifndef STM32U083_PLATFORM_TRANSPORT_H
#define STM32U083_PLATFORM_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#include "stm32u083_platform/PlatformBoard.h"

Platform_Status PlatformTransport_Initialize(void);
size_t PlatformTransport_Read(uint8_t *output, size_t capacity);
Platform_Status PlatformTransport_Write(const uint8_t *data, size_t length);
int PlatformTransport_IsEnabled(void);

#endif
