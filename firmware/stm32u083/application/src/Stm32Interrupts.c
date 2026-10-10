#include "stm32u0xx_hal.h"

#include "FreeRTOS.h"
#include "task.h"

extern void xPortSysTickHandler(void);

void SysTick_Handler(void) {
    HAL_IncTick();
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        xPortSysTickHandler();
    }
}

void HardFault_Handler(void) {
    __disable_irq();
    for (;;) {
    }
}
