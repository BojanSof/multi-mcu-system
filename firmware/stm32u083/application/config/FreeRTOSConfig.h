#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>

extern uint32_t SystemCoreClock;

#define configUSE_PREEMPTION 1
#define configENABLE_MPU 0
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#define configCPU_CLOCK_HZ SystemCoreClock
#define configTICK_RATE_HZ ((TickType_t)1000U)
#define configMAX_PRIORITIES 4U
#define configMINIMAL_STACK_SIZE ((uint16_t)128U)
#define configMAX_TASK_NAME_LEN 12U
#define configUSE_16_BIT_TICKS 0
#define configIDLE_SHOULD_YIELD 1
#define configUSE_IDLE_HOOK 0
#define configUSE_TICK_HOOK 1
#define configUSE_TASK_NOTIFICATIONS 1
#define configUSE_MUTEXES 0
#define configUSE_RECURSIVE_MUTEXES 0
#define configUSE_COUNTING_SEMAPHORES 0
#define configUSE_TIMERS 0
#define configSUPPORT_DYNAMIC_ALLOCATION 0
#define configSUPPORT_STATIC_ALLOCATION 1
#define configCHECK_FOR_STACK_OVERFLOW 2
#define configUSE_MALLOC_FAILED_HOOK 0

#define INCLUDE_vTaskDelay 1
#define INCLUDE_xTaskGetSchedulerState 1
#define INCLUDE_vTaskSuspend 0
#define INCLUDE_vTaskDelete 0

#define configKERNEL_INTERRUPT_PRIORITY 3U
#define vPortSVCHandler SVC_Handler
#define xPortPendSVHandler PendSV_Handler

#endif
