#include "AppRtos.h"

#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "AppFaultHistory.h"
#include "AppServices.h"
#if defined(MULTI_MCU_CUSTOM_BOOT_SLOT)
#include "AppBootControl.h"
#endif
#include "NodeTransport.h"
#include "stm32u083_platform/PlatformBoard.h"
#include "stm32u083_platform/PlatformLog.h"
#include "stm32u083_platform/PlatformSideband.h"
#include "stm32u083_platform/PlatformSupervision.h"
#include "stm32u0xx_hal.h"

#define APP_TASK_STACK_WORDS 768U
#define APP_POLL_INTERVAL_MS 20U
#define APP_HEARTBEAT_INTERVAL_MS 500U
#define APP_HEARTBEAT_POLLS (APP_HEARTBEAT_INTERVAL_MS / APP_POLL_INTERVAL_MS)
#define APP_LOG_HEARTBEAT_COUNT 4U
#define APP_TASK_STALL_MS 6000U
#define APP_WATCHDOG_STACK_WORDS 192U
#define APP_WATCHDOG_POLL_MS 250U

static StaticTask_t AppRtos_TaskControl;
static StackType_t AppRtos_TaskStack[APP_TASK_STACK_WORDS];
static StaticTask_t AppRtos_WatchdogTaskControl;
static StackType_t AppRtos_WatchdogTaskStack[APP_WATCHDOG_STACK_WORDS];
static StaticTask_t AppRtos_IdleTaskControl;
static StackType_t AppRtos_IdleTaskStack[configMINIMAL_STACK_SIZE];
static NodeTransport_State AppRtos_NodeTransport;
static AppServices_State AppRtos_Services;

static void AppRtos_WatchdogTask(void *context) {
    uint32_t last_progress = 0U;
    (void)context;
    if (PlatformSupervision_StartWatchdog() != PLATFORM_STATUS_OK) {
        PlatformSideband_SetReady(false);
        for (;;) {
        }
    }
    for (;;) {
        const uint32_t progress = AppServices_GetProgressCount(&AppRtos_Services);
        if (progress != last_progress) {
            PlatformSupervision_RefreshWatchdog();
            last_progress = progress;
        }
        vTaskDelay(pdMS_TO_TICKS(APP_WATCHDOG_POLL_MS));
    }
}

static void AppRtos_BringupTask(void *context) {
    char log_line[128];
    bool previous_button = false;
    uint32_t heartbeat_polls = 0U;
    uint32_t heartbeat_count = 0U;
    (void)context;
    const Platform_ResetInfo reset_info = PlatformSupervision_GetResetInfo();
    const uint32_t seed = HAL_GetUIDw0() ^ (reset_info.boot_count * 2654435761UL);
    if (AppServices_Initialize(&AppRtos_Services, seed) != NODE_API_STATUS_OK) {
        PlatformBoard_SetLed(true);
        for (;;) {
        }
    }
    NodeTransport_Initialize(&AppRtos_NodeTransport, seed, &AppRtos_Services.registry);
    AppServices_MarkReady(&AppRtos_Services);
#if defined(MULTI_MCU_CUSTOM_BOOT_SLOT)
    /* Reaching the scheduled bring-up task with initialized services is the
     * custom-profile self-test gate. Confirmation is committed by the
     * bootloader after this deliberate reset. */
    AppBootControl_ConfirmTrialIfRequested();
#endif
    AppFaultHistory_Initialize(AppRtos_NodeTransport.session_generation);
    const int startup_length = snprintf(
        log_line, sizeof(log_line),
        "stm32: fw %s phase-5 %s personality %s session %lu reset %u flags 0x%04x boot %lu\r\n",
        MULTI_MCU_FIRMWARE_VERSION,
#if defined(MULTI_MCU_DEV_FAULTS)
        "debug",
#else
        "release",
#endif
        AppServices_GetPersonalityName(), (unsigned long)AppRtos_NodeTransport.session_generation,
        (unsigned int)AppRtos_Services.lifecycle.reset_reason,
        (unsigned int)AppRtos_Services.lifecycle.raw_reset_flags,
        (unsigned long)AppRtos_Services.lifecycle.boot_count);
    if ((startup_length > 0) && ((size_t)startup_length < sizeof(log_line))) {
        (void)PlatformLog_Write(log_line, (size_t)startup_length);
    }

    for (;;) {
        const bool button = PlatformBoard_IsButtonPressed();
        NodeTransport_Process(&AppRtos_NodeTransport);
        AppServices_ApplyReadyState(&AppRtos_Services);
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
        AppServices_RecordProgress(&AppRtos_Services);

        if (AppDiagnostics_ConsumeTaskStall()) {
            (void)PlatformLog_Write("stm32: watchdog stall injection; reset expected\r\n",
                                    sizeof("stm32: watchdog stall injection; reset expected\r\n") -
                                        1U);
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
    task = xTaskCreateStatic(AppRtos_WatchdogTask, "watchdog", APP_WATCHDOG_STACK_WORDS, NULL,
                             tskIDLE_PRIORITY + 2U, AppRtos_WatchdogTaskStack,
                             &AppRtos_WatchdogTaskControl);
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
