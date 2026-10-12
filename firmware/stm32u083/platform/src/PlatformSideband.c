#include "stm32u083_platform/PlatformSideband.h"

#include "stm32u0xx_hal.h"

#if defined(MULTI_MCU_SIDEBAND_PINS_VERIFIED)
void PlatformSideband_ConfigureVerifiedPins(void) {
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOB_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_RESET);
    gpio.Pin = GPIO_PIN_4;
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &gpio);
    gpio.Pin = GPIO_PIN_5;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    HAL_GPIO_Init(GPIOB, &gpio);
}

void PlatformSideband_WriteVerifiedReady(bool high) {
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void PlatformSideband_WriteVerifiedIrqN(bool high) {
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}
#endif

/* Default is safe no-drive until READY and IRQ_N pins are frozen and verified. */
void PlatformSideband_Initialize(void) {
#if defined(MULTI_MCU_SIDEBAND_PINS_VERIFIED)
    PlatformSideband_ConfigureVerifiedPins();
    PlatformSideband_WriteVerifiedReady(false);
    PlatformSideband_WriteVerifiedIrqN(true);
#endif
}

void PlatformSideband_SetReady(bool ready) {
#if defined(MULTI_MCU_SIDEBAND_PINS_VERIFIED)
    PlatformSideband_WriteVerifiedReady(ready);
#else
    (void)ready;
#endif
}

void PlatformSideband_SetIrqPending(bool pending) {
#if defined(MULTI_MCU_SIDEBAND_PINS_VERIFIED)
    PlatformSideband_WriteVerifiedIrqN(!pending);
#else
    (void)pending;
#endif
}

bool PlatformSideband_IsEnabled(void) {
#if defined(MULTI_MCU_SIDEBAND_PINS_VERIFIED)
    return true;
#else
    return false;
#endif
}
