#ifndef STM32U0XX_HAL_CONF_H
#define STM32U0XX_HAL_CONF_H

#define HAL_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED

#define HSE_VALUE 8000000U
#define HSE_STARTUP_TIMEOUT 5000U
#define MSI_VALUE 4000000U
#define MSI32_VALUE 32000000U
#define HSI_VALUE 16000000U
#define HSI48_VALUE 48000000U
#define LSE_VALUE 32768U
#define LSE_STARTUP_TIMEOUT 5000U
#define LSI_VALUE 32000U
#define EXTERNAL_I2S1_CLOCK_VALUE 12288000U
#define VDD_VALUE 3300U
#define TICK_INT_PRIORITY 3U
#define USE_RTOS 0U
#define PREFETCH_ENABLE 0U
#define INSTRUCTION_CACHE_ENABLE 1U

#include "stm32u0xx_hal_cortex.h"
#include "stm32u0xx_hal_def.h"
#include "stm32u0xx_hal_flash.h"
#include "stm32u0xx_hal_gpio.h"
#include "stm32u0xx_hal_pwr.h"
#include "stm32u0xx_hal_rcc.h"
#include "stm32u0xx_hal_uart.h"

#define assert_param(expression) ((void)0U)

#endif
