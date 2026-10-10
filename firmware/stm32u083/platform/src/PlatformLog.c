#include "stm32u083_platform/PlatformLog.h"

#include <stdint.h>

#include "stm32u0xx_hal.h"

#if !defined(STM32U083xx) || !defined(MULTI_MCU_BOARD_NUCLEO_U083RC)
#error "PlatformLog supports only the NUCLEO-U083RC target"
#endif

/* ST's MB1932B BSP routes its independent debug COM port to USART2 PA2/PA3. */
static UART_HandleTypeDef PlatformLog_Uart;

Platform_Status PlatformLog_Initialize(void) {
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_2 | GPIO_PIN_3;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &gpio);

    PlatformLog_Uart.Instance = USART2;
    PlatformLog_Uart.Init.BaudRate = 115200U;
    PlatformLog_Uart.Init.WordLength = UART_WORDLENGTH_8B;
    PlatformLog_Uart.Init.StopBits = UART_STOPBITS_1;
    PlatformLog_Uart.Init.Parity = UART_PARITY_NONE;
    PlatformLog_Uart.Init.Mode = UART_MODE_TX_RX;
    PlatformLog_Uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    PlatformLog_Uart.Init.OverSampling = UART_OVERSAMPLING_16;
    PlatformLog_Uart.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    PlatformLog_Uart.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

    return HAL_UART_Init(&PlatformLog_Uart) == HAL_OK ? PLATFORM_STATUS_OK : PLATFORM_STATUS_ERROR;
}

Platform_Status PlatformLog_Write(const char *message, size_t length) {
    if ((message == NULL) || (length > UINT16_MAX)) {
        return PLATFORM_STATUS_ERROR;
    }
    return HAL_UART_Transmit(&PlatformLog_Uart, (const uint8_t *)message, (uint16_t)length, 100U) ==
                   HAL_OK
               ? PLATFORM_STATUS_OK
               : PLATFORM_STATUS_ERROR;
}
