#include "AppRtos.h"

#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "AppServices.h"
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
#define APP_TASK_STALL_MS 1500U

static StaticTask_t AppRtos_TaskControl;
static StackType_t AppRtos_TaskStack[APP_TASK_STACK_WORDS];
static StaticTask_t AppRtos_IdleTaskControl;
static StackType_t AppRtos_IdleTaskStack[configMINIMAL_STACK_SIZE];
static NodeTransport_State AppRtos_NodeTransport;
static AppServices_State AppRtos_Services;

static void AppRtos_BringupTask(void *context) {
    char log_line[128];
    bool previous_button = false;
    uint32_t heartbeat_polls = 0U;
    uint32_t heartbeat_count = 0U;
    (void)context;
    const uint32_t seed = HAL_GetUIDw0() ^ HAL_GetTick() ^ SysTick->VAL;
    if (AppServices_Initialize(&AppRtos_Services, seed) != NODE_API_STATUS_OK) {
        PlatformBoard_SetLed(true);
        for (;;) {
        }
    }
    NodeTransport_Initialize(&AppRtos_NodeTransport, seed, &AppRtos_Services.registry);
    const int startup_length = snprintf(
        log_line, sizeof(log_line),
        "stm32: fw %s phase-4 %s personality %s session %lu uptime %lu ms\r\n",
        MULTI_MCU_FIRMWARE_VERSION,
#if defined(MULTI_MCU_DEV_FAULTS)
        "debug",
#else
        "release",
#endif
        AppServices_GetPersonalityName(), (unsigned long)AppRtos_NodeTransport.session_generation,
        (unsigned long)HAL_GetTick());
    if ((startup_length > 0) && ((size_t)startup_length < sizeof(log_line))) {
        (void)PlatformLog_Write(log_line, (size_t)startup_length);
    }

    for (;;) {
        const bool button = PlatformBoard_IsButtonPressed();
        NodeTransport_Process(&AppRtos_NodeTransport);
        if (button != previous_button) {
            (void)AppServices_PublishButton(&AppRtos_Services, button);
            (void)PlatformLog_Write(button ? "stm32: button pressed event\r\n"
                                           : "stm32: button released event\r\n",
                                    button ? sizeof("stm32: button pressed event\r\n") - 1U
                                           : sizeof("stm32: button released event\r\n") - 1U);
        }
        previous_button = button;
        (void)AppServices_Tick(&AppRtos_Services, HAL_GetTick());
        AppDiagnostics_ProcessEvents();

        if (AppDiagnostics_ConsumeTaskStall()) {
            (void)PlatformLog_Write("stm32: bounded task stall injection\r\n",
                                    sizeof("stm32: bounded task stall injection\r\n") - 1U);
            vTaskDelay(pdMS_TO_TICKS(APP_TASK_STALL_MS));
        }

        ++heartbeat_polls;
        if (heartbeat_polls >= APP_HEARTBEAT_POLLS) {
            heartbeat_polls = 0U;
            PlatformBoard_ToggleLed();
            ++heartbeat_count;
            if (heartbeat_count >= APP_LOG_HEARTBEAT_COUNT) {
                heartbeat_count = 0U;
                const int heartbeat_length =
                    snprintf(log_line, sizeof(log_line),
                             "stm32: fw %s session %lu uptime %lu ms heartbeat\r\n",
                             MULTI_MCU_FIRMWARE_VERSION,
                             (unsigned long)AppRtos_NodeTransport.session_generation,
                             (unsigned long)HAL_GetTick());
                if ((heartbeat_length > 0) && ((size_t)heartbeat_length < sizeof(log_line))) {
                    (void)PlatformLog_Write(log_line, (size_t)heartbeat_length);
                }
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
