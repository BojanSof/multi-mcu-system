#include "AppBootControl.h"
#include "stm32u083_bootloader/BootConfirm.h"
#include "stm32u0xx_hal.h"
#include <assert.h>
Test_TampRegisters Test_Tamp;
static unsigned Test_Resets;
void HAL_PWR_EnableBkUpAccess(void) {}
void NVIC_SystemReset(void) { ++Test_Resets; }
int main(void) {
    Test_Tamp.BKP0R = 77U;
    Test_Tamp.BKP1R = 0U;
    AppBootControl_ConfirmTrialIfRequested();
    assert(Test_Resets == 0U && Test_Tamp.BKP0R == 77U);
    Test_Tamp.BKP1R = BOOT_CONFIRM_MARKER(BOOT_CONFIRM_TRIAL_BASE, 0U);
    AppBootControl_ConfirmTrialIfRequested();
    assert(Test_Resets == 1U);
#if defined(MULTI_MCU_TRIAL_FAILURE_DEMO)
    assert(Test_Tamp.BKP0R == 77U);
    assert(Test_Tamp.BKP1R == BOOT_CONFIRM_MARKER(BOOT_CONFIRM_TRIAL_BASE, 0U));
#else
    assert(Test_Tamp.BKP0R == BOOT_CONFIRM_MARKER(BOOT_CONFIRM_APPLICATION_BASE, 0U));
    assert(Test_Tamp.BKP1R == 0U);
#endif
    return 0;
}
