#include "AppRtos.h"

#include "FreeRTOS.h"
#include "task.h"

#include "stm32u083_platform/PlatformBoard.h"
#include "stm32u083_platform/PlatformLog.h"

#define APP_TASK_STACK_WORDS 256U

static StaticTask_t AppRtos_TaskControl;
static StackType_t AppRtos_TaskStack[APP_TASK_STACK_WORDS];
static StaticTask_t AppRtos_IdleTaskControl;
static StackType_t AppRtos_IdleTaskStack[configMINIMAL_STACK_SIZE];

static void AppRtos_BringupTask(void *context) {
    bool previous_button = false;
    (void)context;
    (void)PlatformLog_Write("stm32: phase-1 bring-up ready\r\n",
                            sizeof("stm32: phase-1 bring-up ready\r\n") - 1U);

    for (;;) {
        const bool button = PlatformBoard_IsButtonPressed();
        if (button && !previous_button) {
            PlatformBoard_ToggleLed();
            (void)PlatformLog_Write("stm32: user button pressed\r\n",
                                    sizeof("stm32: user button pressed\r\n") - 1U);
        }
        previous_button = button;
        vTaskDelay(pdMS_TO_TICKS(20U));
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
    __disable_irq();
    for (;;) {
    }
}
