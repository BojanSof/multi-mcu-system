#ifndef STM32_HAL_TEST_H
#define STM32_HAL_TEST_H
#include <stdint.h>
uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t milliseconds);
/* Boot confirmation tests substitute backup registers and reset, not flash. */
typedef struct {
    uint32_t BKP0R;
    uint32_t BKP1R;
} Test_TampRegisters;
extern Test_TampRegisters Test_Tamp;
#define TAMP (&Test_Tamp)
#define __HAL_RCC_PWR_CLK_ENABLE() ((void)0)
#define __HAL_RCC_RTCAPB_CLK_ENABLE() ((void)0)
#define __DSB() ((void)0)
void HAL_PWR_EnableBkUpAccess(void);
void NVIC_SystemReset(void);
#endif
