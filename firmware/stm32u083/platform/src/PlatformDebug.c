#include "stm32u083_platform/PlatformDebug.h"

#include "stm32u0xx_hal.h"

#define PLATFORM_DEBUG_PORT GPIOB
#define PLATFORM_DEBUG_PIN GPIO_PIN_3

Platform_Status PlatformDebug_Initialize(void) {
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    HAL_GPIO_WritePin(PLATFORM_DEBUG_PORT, PLATFORM_DEBUG_PIN, GPIO_PIN_RESET);
    gpio.Pin = PLATFORM_DEBUG_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(PLATFORM_DEBUG_PORT, &gpio);
    return PLATFORM_STATUS_OK;
}

void PlatformDebug_Set(bool active) {
    HAL_GPIO_WritePin(PLATFORM_DEBUG_PORT, PLATFORM_DEBUG_PIN,
                      active ? GPIO_PIN_SET : GPIO_PIN_RESET);
}
