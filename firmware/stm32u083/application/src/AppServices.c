#include "AppServices.h"

#include <string.h>

#include "NodeTransport.h"

#if !defined(MULTI_MCU_PERSONALITY_A) && !defined(MULTI_MCU_PERSONALITY_B)
#error "Select exactly one STM32 service personality"
#endif

static AppServices_State *AppServices_Instance;
#if defined(MULTI_MCU_PERSONALITY_A) && defined(MULTI_MCU_PERSONALITY_B)
#error "Select exactly one STM32 service personality"
#endif

static NodeApi_Status AppServices_GpioHandler(void *context, const NodeApi_ServiceRequest *request,
                                              NodeApi_ServiceResponse *response) {
    (void)context;
    (void)request;
    (void)response;
    return NODE_API_STATUS_UNSUPPORTED_COMMAND;
}

static NodeApi_Status AppServices_VirtualEventSink(void *context, const VirtualIo_Event *event) {
    (void)context;
    if (event == NULL) {
        return NODE_API_STATUS_INVALID_ARGUMENT;
    }
    return NodeTransport_PublishServiceEvent(event->service_id, event->event_id,
                                             event->operation_id, event->data, event->data_length);
}

NodeApi_Status AppServices_Initialize(AppServices_State *state, uint32_t seed) {
    NodeApi_ServiceRegistration gpio_service = {
        .descriptor = {.service_id = NODE_API_SERVICE_GPIO,
                       .version_major = 1U,
                       .version_minor = 0U,
                       .flags = 0U},
        .handler = AppServices_GpioHandler,
        .context = state,
    };
    NodeApi_Status status;

    if (state == NULL) {
        return NODE_API_STATUS_INVALID_ARGUMENT;
    }
    (void)memset(state, 0, sizeof(*state));
    AppServices_Instance = state;
    NodeApi_RegistryInitialize(&state->registry);
    status = NodeApi_RegisterService(&state->registry, &gpio_service);
    if (status != NODE_API_STATUS_OK) {
        return status;
    }
    VirtualIo_Initialize(&state->virtual_io, seed, AppServices_VirtualEventSink, state);
    status = AppDiagnostics_Initialize(&state->diagnostics, &state->registry, &state->virtual_io);
    if (status != NODE_API_STATUS_OK) {
        return status;
    }
    status = AppLifecycle_Initialize(&state->lifecycle, &state->registry);
    if (status != NODE_API_STATUS_OK) {
        return status;
    }
    AppDiagnostics_RecordReset((uint8_t)state->lifecycle.reset_reason,
                               state->lifecycle.raw_reset_flags);
#if defined(MULTI_MCU_PERSONALITY_B)
    status =
        VirtualIo_RegisterServices(&state->virtual_io, &state->registry, VIRTUAL_IO_PERSONALITY_B);
#else
    status =
        VirtualIo_RegisterServices(&state->virtual_io, &state->registry, VIRTUAL_IO_PERSONALITY_A);
#endif
#if defined(MULTI_MCU_INCOMPATIBLE_SENSOR_DEMO)
    if (status == NODE_API_STATUS_OK) {
        size_t index;

        for (index = 0U; index < state->registry.service_count; ++index) {
            if (state->registry.services[index].descriptor.service_id ==
                VIRTUAL_IO_SENSOR_SERVICE_ID) {
                state->registry.services[index].descriptor.version_major++;
                break;
            }
        }
    }
#endif
    return status;
}

NodeApi_Status AppServices_Tick(AppServices_State *state, uint32_t now_ms) {
    if (state == NULL) {
        return NODE_API_STATUS_INVALID_ARGUMENT;
    }
    if (state->lifecycle.state == NODE_API_LIFECYCLE_SHUTDOWN) {
        return NODE_API_STATUS_OK;
    }
    VirtualIo_SetEarlyOutputInjection(
        &state->virtual_io,
        AppDiagnostics_IsInjectionEnabled(DIAGNOSTICS_INJECTION_EARLY_VIRTUAL_OUTPUT));
    return VirtualIo_Tick(&state->virtual_io, now_ms);
}

NodeApi_Status AppServices_PublishButton(AppServices_State *state, bool pressed) {
    uint8_t event_data[NODE_API_GPIO_BUTTON_EVENT_DATA_SIZE];
    if (state == NULL) {
        return NODE_API_STATUS_INVALID_ARGUMENT;
    }
    if (state->lifecycle.state == NODE_API_LIFECYCLE_SHUTDOWN) {
        return NODE_API_STATUS_BUSY;
    }
    ++state->button_event_counter;
    if (state->button_event_counter == 0U) {
        ++state->button_event_counter;
    }
    event_data[0] = pressed ? 1U : 0U;
    return NodeTransport_PublishServiceEvent(
        NODE_API_SERVICE_GPIO, NODE_API_GPIO_EVENT_BUTTON_CHANGED, state->button_event_counter,
        event_data, sizeof(event_data));
}

void AppServices_RecordProgress(AppServices_State *state) {
    if (state != NULL) {
        AppLifecycle_RecordProgress(&state->lifecycle);
    }
}

uint32_t AppServices_GetProgressCount(const AppServices_State *state) {
    return state == NULL ? 0U : state->lifecycle.progress_count;
}

bool AppServices_IsMaintenance(const AppServices_State *state) {
    return (state != NULL) && AppLifecycle_IsMaintenance(&state->lifecycle);
}

bool AppServices_IsMaintenanceActive(void) {
    return AppServices_IsMaintenance(AppServices_Instance);
}

bool AppServices_IsQuiescentActive(void) {
    return (AppServices_Instance != NULL) &&
           AppLifecycle_IsQuiescent(&AppServices_Instance->lifecycle);
}

void AppServices_MarkReady(AppServices_State *state) {
    if (state != NULL) {
        AppLifecycle_MarkReady(&state->lifecycle);
    }
}

void AppServices_MarkOnline(void) {
    if (AppServices_Instance != NULL) {
        AppLifecycle_MarkOnline(&AppServices_Instance->lifecycle);
    }
}

void AppServices_ApplyReadyState(AppServices_State *state) {
    if (state != NULL) {
        AppLifecycle_ApplyReadyState(&state->lifecycle);
    }
}

const char *AppServices_GetPersonalityName(void) {
#if defined(MULTI_MCU_PERSONALITY_B)
    return "B";
#else
    return "A";
#endif
}
