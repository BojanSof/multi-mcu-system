#include "stm32u0xx_hal.h"

void HardFault_Handler(void) {
    __disable_irq();
    for (;;) {
    }
}
