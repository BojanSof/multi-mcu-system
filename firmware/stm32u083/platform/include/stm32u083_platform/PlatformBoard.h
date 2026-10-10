#ifndef STM32U083_PLATFORM_BOARD_H
#define STM32U083_PLATFORM_BOARD_H

#include <stdbool.h>

typedef enum { PLATFORM_STATUS_OK = 0, PLATFORM_STATUS_ERROR = 1 } Platform_Status;

Platform_Status PlatformBoard_Initialize(void);
void PlatformBoard_SetLed(bool enabled);
void PlatformBoard_ToggleLed(void);
bool PlatformBoard_IsButtonPressed(void);

#endif
