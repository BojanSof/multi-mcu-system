#include "stm32u083_platform/PlatformTransport.h"

#include <limits.h>

#include "stm32u0xx_hal.h"

static UART_HandleTypeDef PlatformTransport_Uart;
static int PlatformTransport_Enabled;
#define PLATFORM_TRANSPORT_RX_CAPACITY 512U
#define PLATFORM_TRANSPORT_RX_MASK (PLATFORM_TRANSPORT_RX_CAPACITY - 1U)
#define PLATFORM_TRANSPORT_ISR_DRAIN_LIMIT 32U

_Static_assert((PLATFORM_TRANSPORT_RX_CAPACITY & PLATFORM_TRANSPORT_RX_MASK) == 0U,
               "RX ring capacity must be a power of two");

static uint8_t PlatformTransport_RxRing[PLATFORM_TRANSPORT_RX_CAPACITY];
static volatile uint16_t PlatformTransport_RxHead;
static volatile uint16_t PlatformTransport_RxTail;
static volatile uint32_t PlatformTransport_RxErrors;

void PlatformTransport_ConfigureVerifiedPins(void) {
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_9 | GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &gpio);
}

Platform_Status PlatformTransport_Initialize(void) {
#if !defined(MULTI_MCU_UART1_PINS_VERIFIED)
    PlatformTransport_Enabled = 0;
    return PLATFORM_STATUS_ERROR;
#else
    PlatformTransport_ConfigureVerifiedPins();
    __HAL_RCC_USART1_CLK_ENABLE();
    PlatformTransport_Uart.Instance = USART1;
    PlatformTransport_Uart.Init.BaudRate = 115200U;
    PlatformTransport_Uart.Init.WordLength = UART_WORDLENGTH_8B;
    PlatformTransport_Uart.Init.StopBits = UART_STOPBITS_1;
    PlatformTransport_Uart.Init.Parity = UART_PARITY_NONE;
    PlatformTransport_Uart.Init.Mode = UART_MODE_TX_RX;
    PlatformTransport_Uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    PlatformTransport_Uart.Init.OverSampling = UART_OVERSAMPLING_16;
    PlatformTransport_Uart.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    PlatformTransport_Uart.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    PlatformTransport_Enabled = HAL_UART_Init(&PlatformTransport_Uart) == HAL_OK;
    if (PlatformTransport_Enabled) {
        PlatformTransport_RxHead = 0U;
        PlatformTransport_RxTail = 0U;
        PlatformTransport_RxErrors = 0U;
        HAL_NVIC_SetPriority(USART1_IRQn, 3U, 0U);
        HAL_NVIC_EnableIRQ(USART1_IRQn);
        __HAL_UART_ENABLE_IT(&PlatformTransport_Uart, UART_IT_RXNE);
    }
    return PlatformTransport_Enabled ? PLATFORM_STATUS_OK : PLATFORM_STATUS_ERROR;
#endif
}

size_t PlatformTransport_Read(uint8_t *output, size_t capacity) {
    size_t received = 0U;
    if ((output == NULL) || !PlatformTransport_Enabled) {
        return 0U;
    }
    while ((received < capacity) && (PlatformTransport_RxTail != PlatformTransport_RxHead)) {
        output[received++] = PlatformTransport_RxRing[PlatformTransport_RxTail];
        PlatformTransport_RxTail =
            (uint16_t)((PlatformTransport_RxTail + 1U) & PLATFORM_TRANSPORT_RX_MASK);
    }
    return received;
}

Platform_Status PlatformTransport_Write(const uint8_t *data, size_t length) {
    if ((data == NULL) || !PlatformTransport_Enabled || (length > UINT16_MAX)) {
        return PLATFORM_STATUS_ERROR;
    }
    return HAL_UART_Transmit(&PlatformTransport_Uart, data, (uint16_t)length, 100U) == HAL_OK
               ? PLATFORM_STATUS_OK
               : PLATFORM_STATUS_ERROR;
}

int PlatformTransport_IsEnabled(void) { return PlatformTransport_Enabled; }

void USART1_IRQHandler(void) {
    uint32_t drained = 0U;

    while (drained < PLATFORM_TRANSPORT_ISR_DRAIN_LIMIT) {
        const uint32_t status = PlatformTransport_Uart.Instance->ISR;
        const uint32_t errors =
            status & (USART_ISR_PE | USART_ISR_FE | USART_ISR_NE | USART_ISR_ORE);

        if (errors != 0U) {
            PlatformTransport_Uart.Instance->ICR =
                USART_ICR_PECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_ORECF;
            ++PlatformTransport_RxErrors;
        }
        if ((status & USART_ISR_RXNE_RXFNE) == 0U) {
            break;
        }

        const uint8_t byte = (uint8_t)PlatformTransport_Uart.Instance->RDR;
        const uint16_t next_head =
            (uint16_t)((PlatformTransport_RxHead + 1U) & PLATFORM_TRANSPORT_RX_MASK);
        if (next_head != PlatformTransport_RxTail) {
            PlatformTransport_RxRing[PlatformTransport_RxHead] = byte;
            PlatformTransport_RxHead = next_head;
        } else {
            ++PlatformTransport_RxErrors;
        }
        ++drained;
    }
}
