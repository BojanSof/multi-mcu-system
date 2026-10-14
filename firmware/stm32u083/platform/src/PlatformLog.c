#include "stm32u083_platform/PlatformLog.h"

#include "stm32u0xx_hal.h"
#include <stdint.h>

#if !defined(STM32U083xx) || !defined(MULTI_MCU_BOARD_NUCLEO_U083RC)
#error "PlatformLog supports only the NUCLEO-U083RC target"
#endif

/* ST's MB1932B BSP routes its independent debug COM port to USART2 PA2/PA3. */
static UART_HandleTypeDef PlatformLog_Uart;
#define PLATFORM_LOG_TX_CAPACITY 512U
#define PLATFORM_LOG_TX_MASK (PLATFORM_LOG_TX_CAPACITY - 1U)

_Static_assert((PLATFORM_LOG_TX_CAPACITY & PLATFORM_LOG_TX_MASK) == 0U,
               "log TX ring capacity must be a power of two");

static uint8_t PlatformLog_TxRing[PLATFORM_LOG_TX_CAPACITY];
static volatile uint16_t PlatformLog_TxHead;
static volatile uint16_t PlatformLog_TxTail;
static volatile uint32_t PlatformLog_DroppedMessages;

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

    if (HAL_UART_Init(&PlatformLog_Uart) != HAL_OK) {
        return PLATFORM_STATUS_ERROR;
    }
    PlatformLog_TxHead = 0U;
    PlatformLog_TxTail = 0U;
    PlatformLog_DroppedMessages = 0U;
    HAL_NVIC_SetPriority(USART2_LPUART2_IRQn, 3U, 0U);
    HAL_NVIC_EnableIRQ(USART2_LPUART2_IRQn);
    return PLATFORM_STATUS_OK;
}

Platform_Status PlatformLog_Write(const char *message, size_t length) {
    size_t index;
    uint32_t interrupt_state;
    uint16_t free_space;

    if ((message == NULL) || (length == 0U) || (length >= PLATFORM_LOG_TX_CAPACITY)) {
        return PLATFORM_STATUS_ERROR;
    }
    interrupt_state = __get_PRIMASK();
    __disable_irq();
    free_space = (uint16_t)((PlatformLog_TxTail - PlatformLog_TxHead - 1U) & PLATFORM_LOG_TX_MASK);
    if (length > free_space) {
        if (PlatformLog_DroppedMessages != UINT32_MAX) {
            ++PlatformLog_DroppedMessages;
        }
        if (interrupt_state == 0U) {
            __enable_irq();
        }
        return PLATFORM_STATUS_ERROR;
    }
    for (index = 0U; index < length; ++index) {
        PlatformLog_TxRing[PlatformLog_TxHead] = (uint8_t)message[index];
        PlatformLog_TxHead = (uint16_t)((PlatformLog_TxHead + 1U) & PLATFORM_LOG_TX_MASK);
    }
    __HAL_UART_ENABLE_IT(&PlatformLog_Uart, UART_IT_TXE);
    if (interrupt_state == 0U) {
        __enable_irq();
    }
    return PLATFORM_STATUS_OK;
}

void USART2_LPUART2_IRQHandler(void) {
    if (((PlatformLog_Uart.Instance->ISR & USART_ISR_TXE_TXFNF) != 0U) &&
        (__HAL_UART_GET_IT_SOURCE(&PlatformLog_Uart, UART_IT_TXE) != 0U)) {
        if (PlatformLog_TxTail == PlatformLog_TxHead) {
            __HAL_UART_DISABLE_IT(&PlatformLog_Uart, UART_IT_TXE);
        } else {
            PlatformLog_Uart.Instance->TDR = PlatformLog_TxRing[PlatformLog_TxTail];
            PlatformLog_TxTail = (uint16_t)((PlatformLog_TxTail + 1U) & PLATFORM_LOG_TX_MASK);
        }
    }
}
