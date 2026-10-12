#ifndef STM32U083_PLATFORM_SIDEBAND_H
#define STM32U083_PLATFORM_SIDEBAND_H

#include <stdbool.h>

void PlatformSideband_Initialize(void);
void PlatformSideband_SetReady(bool ready);
/* pending=true drives the verified IRQ_N adapter low. */
void PlatformSideband_SetIrqPending(bool pending);
bool PlatformSideband_IsEnabled(void);

#endif
