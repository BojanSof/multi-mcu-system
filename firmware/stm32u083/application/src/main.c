#include "AppRtos.h"

#include "stm32u083_platform/PlatformBoard.h"
#include "stm32u083_platform/PlatformLog.h"
#include "stm32u083_platform/PlatformSupervision.h"
#include "stm32u0xx_hal.h"

int main(void) {
#if defined(MULTI_MCU_CUSTOM_BOOT_SLOT)
    /* The vendor SystemInit uses the factory flash base. Relocate before any
     * peripheral or RTOS interrupt can be enabled. */
    SCB->VTOR = (uint32_t)MULTI_MCU_SLOT_VECTOR_ADDRESS;
    __DSB();
    __ISB();
#endif
    PlatformSupervision_CaptureReset();
    if (HAL_Init() != HAL_OK) {
        for (;;) {
        }
    }

    if (PlatformBoard_ConfigureClock() != PLATFORM_STATUS_OK) {
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
