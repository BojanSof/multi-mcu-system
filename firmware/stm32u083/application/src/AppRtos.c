#include "AppRtos.h"

#include "FreeRTOS.h"
#include "task.h"

#include "NodeTransport.h"
#include "stm32u083_platform/PlatformBoard.h"
#include "stm32u083_platform/PlatformLog.h"
#include "stm32u083_platform/PlatformSideband.h"
#include "stm32u0xx_hal.h"

#define APP_TASK_STACK_WORDS 768U
#define APP_POLL_INTERVAL_MS 20U
#define APP_HEARTBEAT_INTERVAL_MS 500U
#define APP_HEARTBEAT_POLLS (APP_HEARTBEAT_INTERVAL_MS / APP_POLL_INTERVAL_MS)
#define APP_LOG_HEARTBEAT_COUNT 4U

static StaticTask_t AppRtos_TaskControl;
static StackType_t AppRtos_TaskStack[APP_TASK_STACK_WORDS];
static StaticTask_t AppRtos_IdleTaskControl;
static StackType_t AppRtos_IdleTaskStack[configMINIMAL_STACK_SIZE];
static NodeTransport_State AppRtos_NodeTransport;

static void AppRtos_BringupTask(void *context) {
    bool previous_button = false;
    uint32_t heartbeat_polls = 0U;
    uint32_t heartbeat_count = 0U;
    (void)context;
    NodeTransport_Initialize(&AppRtos_NodeTransport, HAL_GetUIDw0() ^ HAL_GetTick() ^ SysTick->VAL);
    (void)PlatformLog_Write("stm32: phase-2 node ready\r\n",
                            sizeof("stm32: phase-2 node ready\r\n") - 1U);

    for (;;) {
        const bool button = PlatformBoard_IsButtonPressed();
        if (button && !previous_button) {
            (void)PlatformLog_Write("stm32: user button pressed\r\n",
                                    sizeof("stm32: user button pressed\r\n") - 1U);
        }
        previous_button = button;
        NodeTransport_Process(&AppRtos_NodeTransport);

        ++heartbeat_polls;
        if (heartbeat_polls >= APP_HEARTBEAT_POLLS) {
            heartbeat_polls = 0U;
            PlatformBoard_ToggleLed();
            ++heartbeat_count;
            if (heartbeat_count >= APP_LOG_HEARTBEAT_COUNT) {
                heartbeat_count = 0U;
                (void)PlatformLog_Write("stm32: phase-2 heartbeat\r\n",
                                        sizeof("stm32: phase-2 heartbeat\r\n") - 1U);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(APP_POLL_INTERVAL_MS));
    }
}

void AppRtos_Start(void) {
    TaskHandle_t task =
        xTaskCreateStatic(AppRtos_BringupTask, "bringup", APP_TASK_STACK_WORDS, NULL,
                          tskIDLE_PRIORITY + 1U, AppRtos_TaskStack, &AppRtos_TaskControl);
    if (task == NULL) {
        for (;;) {
        }
    }
    vTaskStartScheduler();
    for (;;) {
    }
}

void vApplicationGetIdleTaskMemory(StaticTask_t **task_buffer, StackType_t **stack_buffer,
                                   configSTACK_DEPTH_TYPE *stack_size) {
    *task_buffer = &AppRtos_IdleTaskControl;
    *stack_buffer = AppRtos_IdleTaskStack;
    *stack_size = configMINIMAL_STACK_SIZE;
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name) {
    (void)task;
    (void)task_name;
    PlatformSideband_SetReady(false);
    PlatformBoard_SetLed(true);
    taskDISABLE_INTERRUPTS();
    for (;;) {
    }
}

void vApplicationTickHook(void) { HAL_IncTick(); }
