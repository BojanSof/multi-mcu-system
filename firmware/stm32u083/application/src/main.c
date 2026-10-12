#include "AppRtos.h"

#include "stm32u083_platform/PlatformBoard.h"
#include "stm32u083_platform/PlatformLog.h"
#include "stm32u0xx_hal.h"

int main(void) {
    if (HAL_Init() != HAL_OK) {
        for (;;) {
        }
    }

    /*
     * HAL_Init() enables SysTick before the FreeRTOS lists exist. This image
     * uses the FreeRTOS SysTick handler, so leave its interrupt suspended
     * until xPortStartScheduler() configures and enables the kernel tick.
     */
    HAL_SuspendTick();

    if ((PlatformBoard_Initialize() != PLATFORM_STATUS_OK) ||
        (PlatformLog_Initialize() != PLATFORM_STATUS_OK)) {
        for (;;) {
        }
    }

    AppRtos_Start();
    for (;;) {
    }
}
