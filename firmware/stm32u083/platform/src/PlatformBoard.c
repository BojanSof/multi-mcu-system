#include "stm32u083_platform/PlatformBoard.h"

#include "stm32u0xx_hal.h"

#if !defined(STM32U083xx) || !defined(MULTI_MCU_BOARD_NUCLEO_U083RC)
#error "PlatformBoard supports only the NUCLEO-U083RC target"
#endif

/* Values match ST's STM32U0xx Nucleo BSP for board MB1932B. */
#define PLATFORM_LED_PORT GPIOA
#define PLATFORM_LED_PIN GPIO_PIN_5
#define PLATFORM_BUTTON_PORT GPIOC
#define PLATFORM_BUTTON_PIN GPIO_PIN_13

Platform_Status PlatformBoard_ConfigureClock(void) {
    RCC_OscInitTypeDef oscillator = {0};
    RCC_ClkInitTypeDef clocks = {0};

    oscillator.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    oscillator.HSIState = RCC_HSI_ON;
    oscillator.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    oscillator.PLL.PLLState = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&oscillator) != HAL_OK) {
        return PLATFORM_STATUS_ERROR;
    }

    clocks.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1;
    clocks.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    clocks.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clocks.APB1CLKDivider = RCC_HCLK_DIV1;
    return HAL_RCC_ClockConfig(&clocks, FLASH_LATENCY_0) == HAL_OK ? PLATFORM_STATUS_OK
                                                                   : PLATFORM_STATUS_ERROR;
}

Platform_Status PlatformBoard_Initialize(void) {
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    HAL_GPIO_WritePin(PLATFORM_LED_PORT, PLATFORM_LED_PIN, GPIO_PIN_RESET);
    gpio.Pin = PLATFORM_LED_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(PLATFORM_LED_PORT, &gpio);

    gpio.Pin = PLATFORM_BUTTON_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(PLATFORM_BUTTON_PORT, &gpio);

    return PLATFORM_STATUS_OK;
}

void PlatformBoard_SetLed(bool enabled) {
    HAL_GPIO_WritePin(PLATFORM_LED_PORT, PLATFORM_LED_PIN, enabled ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void PlatformBoard_ToggleLed(void) { HAL_GPIO_TogglePin(PLATFORM_LED_PORT, PLATFORM_LED_PIN); }

bool PlatformBoard_IsButtonPressed(void) {
    return HAL_GPIO_ReadPin(PLATFORM_BUTTON_PORT, PLATFORM_BUTTON_PIN) == GPIO_PIN_RESET;
}
