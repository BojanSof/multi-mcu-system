#include "AppBootControl.h"

#include "stm32u083_bootloader/BootConfirm.h"
#include "stm32u0xx_hal.h"

void AppBootControl_ConfirmTrialIfRequested(void) {
    const uint32_t trial_marker =
        BOOT_CONFIRM_MARKER(BOOT_CONFIRM_TRIAL_BASE, MULTI_MCU_CUSTOM_BOOT_SLOT);
    const uint32_t confirm_marker =
        BOOT_CONFIRM_MARKER(BOOT_CONFIRM_APPLICATION_BASE, MULTI_MCU_CUSTOM_BOOT_SLOT);

    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
    __HAL_RCC_RTCAPB_CLK_ENABLE();
    if (TAMP->BKP1R != trial_marker) {
        return;
    }
#if defined(MULTI_MCU_TRIAL_FAILURE_DEMO)
    /* Explicit development profile: fail self-test before confirmation. */
    NVIC_SystemReset();
    return;
#endif
    TAMP->BKP1R = 0U;
    TAMP->BKP0R = confirm_marker;
    __DSB();
    NVIC_SystemReset();
}
