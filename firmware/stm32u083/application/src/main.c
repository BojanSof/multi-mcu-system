#include "AppRtos.h"

#include "stm32u083_platform/PlatformBoard.h"
#include "stm32u083_platform/PlatformLog.h"
#include "stm32u0xx_hal.h"

int main(void) {
    if (HAL_Init() != HAL_OK) {
        for (;;) {
        }
    }
    if ((PlatformBoard_Initialize() != PLATFORM_STATUS_OK) ||
        (PlatformLog_Initialize() != PLATFORM_STATUS_OK)) {
        for (;;) {
        }
    }

    AppRtos_Start();
    for (;;) {
    }
}
