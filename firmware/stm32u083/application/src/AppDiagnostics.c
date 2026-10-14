#include "AppDiagnostics.h"

#include <limits.h>

#include "NodeTransport.h"
#include "stm32u083_platform/PlatformDebug.h"
#include "stm32u0xx_hal.h"
#include "virtual_io/VirtualIo.h"

#define APP_DIAGNOSTICS_STATUS_SIZE 13U
#define APP_DIAGNOSTICS_RECORD_SIZE 17U
#define APP_DIAGNOSTICS_DELAY_MS 650U
#define APP_DIAGNOSTICS_CODE_INJECTION_ENABLED 0x0100U
#define APP_DIAGNOSTICS_CODE_ACCEPTANCE_DROPPED 0x0101U
#define APP_DIAGNOSTICS_CODE_OUTPUT_ORDER 0x0102U

static AppDiagnostics_State *AppDiagnostics_Instance;

static void AppDiagnostics_MarkEvent(void) {
    PlatformDebug_Set(true);
    if (AppDiagnostics_Instance != NULL) {
        AppDiagnostics_Instance->debug_marker_hold_cycles = 2U;
    }
}

static void AppDiagnostics_WriteU16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
}

static void AppDiagnostics_WriteU32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
    bytes[2] = (uint8_t)(value >> 16U);
    bytes[3] = (uint8_t)(value >> 24U);
}

static void AppDiagnostics_SerializeRecord(uint8_t *data, const Diagnostics_Fault *fault) {
    AppDiagnostics_WriteU32(&data[0], fault->timestamp_ms);
    AppDiagnostics_WriteU32(&data[4], fault->correlation_id);
    AppDiagnostics_WriteU32(&data[8], fault->detail);
    AppDiagnostics_WriteU16(&data[12], fault->code);
    data[14] = fault->source;
    data[15] = fault->severity;
    data[16] = fault->domain;
}

static void AppDiagnostics_Record(uint16_t code, Diagnostics_Severity severity,
                                  Diagnostics_Domain domain, uint32_t correlation_id,
                                  uint32_t detail) {
    Diagnostics_Fault fault = {.timestamp_ms = HAL_GetTick(),
                               .correlation_id = correlation_id,
                               .detail = detail,
                               .code = code,
                               .source = DIAGNOSTICS_SOURCE_NODE,
                               .severity = (uint8_t)severity,
                               .domain = (uint8_t)domain};
    if (AppDiagnostics_Instance == NULL) {
        return;
    }
    (void)Diagnostics_RecordFault(&AppDiagnostics_Instance->diagnostics, &fault);
    AppDiagnostics_Instance->pending_record_event = fault;
    AppDiagnostics_Instance->record_event_pending = true;
}

static void AppDiagnostics_WriteStatus(const AppDiagnostics_State *state,
                                       NodeApi_ServiceResponse *response) {
    AppDiagnostics_WriteU32(&response->data[0], state->diagnostics.injection_mask);
    AppDiagnostics_WriteU32(&response->data[4], state->diagnostics.total_recorded);
    AppDiagnostics_WriteU32(&response->data[8], state->diagnostics.overflow_count);
    response->data[12] = (uint8_t)Diagnostics_GetCount(&state->diagnostics);
    response->data_length = APP_DIAGNOSTICS_STATUS_SIZE;
}

static NodeApi_Status AppDiagnostics_Enable(AppDiagnostics_State *state,
                                            const NodeApi_ServiceRequest *request,
                                            NodeApi_ServiceResponse *response) {
#if defined(MULTI_MCU_DEV_FAULTS)
    Diagnostics_InjectionId injection;
    if (request->data_length != 1U) {
        return NODE_API_STATUS_INVALID_ARGUMENT;
    }
    injection = (Diagnostics_InjectionId)request->data[0];
    if (!Diagnostics_EnableInjection(&state->diagnostics, injection)) {
        return NODE_API_STATUS_INVALID_ARGUMENT;
    }
    if (injection == DIAGNOSTICS_INJECTION_DROP_ACCEPTANCE) {
        state->drop_acceptance_pending = true;
    } else if (injection == DIAGNOSTICS_INJECTION_CORRUPT_UART) {
        state->corrupt_pending = true;
    } else if (injection == DIAGNOSTICS_INJECTION_TRUNCATE_UART) {
        state->truncate_pending = true;
    } else if (injection == DIAGNOSTICS_INJECTION_TASK_STALL) {
        state->task_stall_pending = true;
    }
    AppDiagnostics_Record(APP_DIAGNOSTICS_CODE_INJECTION_ENABLED, DIAGNOSTICS_SEVERITY_WARNING,
                          DIAGNOSTICS_DOMAIN_RUNTIME, request->operation_id, (uint32_t)injection);
    AppDiagnostics_WriteStatus(state, response);
    return NODE_API_STATUS_OK;
#else
    (void)state;
    (void)request;
    (void)response;
    return NODE_API_STATUS_UNSUPPORTED_COMMAND;
#endif
}

static NodeApi_Status AppDiagnostics_Handler(void *context, const NodeApi_ServiceRequest *request,
                                             NodeApi_ServiceResponse *response) {
    AppDiagnostics_State *state = (AppDiagnostics_State *)context;
    Diagnostics_Fault fault;
    if ((state == NULL) || (request == NULL) || (response == NULL)) {
        return NODE_API_STATUS_INVALID_ARGUMENT;
    }
    switch (request->command_id) {
    case NODE_API_DIAGNOSTICS_COMMAND_GET_STATUS:
        if (request->data_length != 0U) {
            return NODE_API_STATUS_INVALID_ARGUMENT;
        }
        AppDiagnostics_WriteStatus(state, response);
        return NODE_API_STATUS_OK;
    case NODE_API_DIAGNOSTICS_COMMAND_ENABLE_INJECTION:
        return AppDiagnostics_Enable(state, request, response);
    case NODE_API_DIAGNOSTICS_COMMAND_DISABLE_ALL:
        if (request->data_length != 0U) {
            return NODE_API_STATUS_INVALID_ARGUMENT;
        }
        Diagnostics_DisableAllInjections(&state->diagnostics);
        state->drop_acceptance_pending = false;
        state->task_stall_pending = false;
        state->corrupt_pending = false;
        state->truncate_pending = false;
        AppDiagnostics_WriteStatus(state, response);
        return NODE_API_STATUS_OK;
    case NODE_API_DIAGNOSTICS_COMMAND_GET_RECORD:
        if (request->data_length != 0U) {
            return NODE_API_STATUS_INVALID_ARGUMENT;
        }
        response->data[0] = Diagnostics_PopFault(&state->diagnostics, &fault) ? 1U : 0U;
        response->data_length = 1U;
        if (response->data[0] != 0U) {
            AppDiagnostics_SerializeRecord(&response->data[1], &fault);
            response->data_length += APP_DIAGNOSTICS_RECORD_SIZE;
        }
        return NODE_API_STATUS_OK;
    default:
        return NODE_API_STATUS_UNSUPPORTED_COMMAND;
    }
}

NodeApi_Status AppDiagnostics_Initialize(AppDiagnostics_State *state, NodeApi_Registry *registry,
                                         VirtualIo_Instance *virtual_io) {
    NodeApi_ServiceRegistration service = {
        .descriptor = {.service_id = NODE_API_SERVICE_DIAGNOSTICS,
                       .version_major = DIAGNOSTICS_API_VERSION_MAJOR,
                       .version_minor = DIAGNOSTICS_API_VERSION_MINOR,
                       .flags = 0U},
        .handler = AppDiagnostics_Handler,
        .context = state,
    };
    if ((state == NULL) || (registry == NULL) || (virtual_io == NULL)) {
        return NODE_API_STATUS_INVALID_ARGUMENT;
    }
    Diagnostics_Initialize(&state->diagnostics);
    state->drop_acceptance_pending = false;
    state->task_stall_pending = false;
    state->corrupt_pending = false;
    state->truncate_pending = false;
    state->record_event_pending = false;
    state->debug_marker_hold_cycles = 0U;
    state->last_output_operation_id = 0U;
    state->virtual_io = virtual_io;
    if (PlatformDebug_Initialize() != PLATFORM_STATUS_OK) {
        return NODE_API_STATUS_INVALID_ARGUMENT;
    }
    AppDiagnostics_Instance = state;
    return NodeApi_RegisterService(registry, &service);
}

bool AppDiagnostics_IsInjectionEnabled(Diagnostics_InjectionId injection) {
#if defined(MULTI_MCU_DEV_FAULTS)
    return (AppDiagnostics_Instance != NULL) &&
           Diagnostics_IsInjectionEnabled(&AppDiagnostics_Instance->diagnostics, injection);
#else
    (void)injection;
    return false;
#endif
}

bool AppDiagnostics_ShouldDropAcceptance(uint16_t service_id, uint16_t command_id,
                                         uint32_t operation_id) {
    if ((AppDiagnostics_Instance == NULL) || !AppDiagnostics_Instance->drop_acceptance_pending ||
        (service_id != VIRTUAL_IO_ACTUATOR_SERVICE_ID) ||
        (command_id != VIRTUAL_IO_ACTUATOR_COMMAND_START)) {
        return false;
    }
    AppDiagnostics_Instance->drop_acceptance_pending = false;
    AppDiagnostics_Record(APP_DIAGNOSTICS_CODE_ACCEPTANCE_DROPPED, DIAGNOSTICS_SEVERITY_WARNING,
                          DIAGNOSTICS_DOMAIN_TRANSPORT, operation_id, 1U);
    AppDiagnostics_MarkEvent();
    return true;
}

void AppDiagnostics_PrepareOutgoing(uint8_t *encoded, size_t *encoded_length) {
    if ((AppDiagnostics_Instance == NULL) || (encoded == NULL) || (encoded_length == NULL)) {
        return;
    }
    if (AppDiagnostics_Instance->corrupt_pending && (*encoded_length > 3U)) {
        encoded[1] ^= 0x01U;
        AppDiagnostics_Instance->corrupt_pending = false;
    }
    if (AppDiagnostics_Instance->truncate_pending && (*encoded_length > 2U)) {
        *encoded_length /= 2U;
        AppDiagnostics_Instance->truncate_pending = false;
    }
}

void AppDiagnostics_DelayResponse(void) {
    if (AppDiagnostics_Instance == NULL) {
        return;
    }
    if (AppDiagnostics_IsInjectionEnabled(DIAGNOSTICS_INJECTION_DELAY_RESPONSE)) {
        HAL_Delay(APP_DIAGNOSTICS_DELAY_MS);
    }
}

bool AppDiagnostics_ConsumeTaskStall(void) {
    if ((AppDiagnostics_Instance == NULL) || !AppDiagnostics_Instance->task_stall_pending) {
        return false;
    }
    AppDiagnostics_Instance->task_stall_pending = false;
    return true;
}

void AppDiagnostics_RecordOutputOrdering(uint32_t operation_id) {
    VirtualIo_OutputState output_state;
    bool violated;

    if ((AppDiagnostics_Instance == NULL) ||
        (AppDiagnostics_Instance->last_output_operation_id == operation_id)) {
        return;
    }
    if (VirtualIo_GetOutputState(AppDiagnostics_Instance->virtual_io, &output_state) !=
        NODE_API_STATUS_OK) {
        return;
    }
    AppDiagnostics_Instance->last_output_operation_id = operation_id;
    violated = output_state.output_enable_order < output_state.prerequisite_order;
    /* detail bit 0: prerequisite-first; bit 1: output-first bug reproduced. */
    AppDiagnostics_Record(APP_DIAGNOSTICS_CODE_OUTPUT_ORDER,
                          violated ? DIAGNOSTICS_SEVERITY_WARNING : DIAGNOSTICS_SEVERITY_INFO,
                          DIAGNOSTICS_DOMAIN_SIMULATION, operation_id, violated ? 2U : 1U);
    AppDiagnostics_MarkEvent();
}

void AppDiagnostics_ProcessEvents(void) {
    uint8_t event_data[APP_DIAGNOSTICS_RECORD_SIZE];
    if (AppDiagnostics_Instance == NULL) {
        return;
    }
    if (AppDiagnostics_Instance->debug_marker_hold_cycles != 0U) {
        --AppDiagnostics_Instance->debug_marker_hold_cycles;
        if (AppDiagnostics_Instance->debug_marker_hold_cycles == 0U) {
            PlatformDebug_Set(false);
        }
    }
    if (!AppDiagnostics_Instance->record_event_pending) {
        return;
    }
    AppDiagnostics_SerializeRecord(event_data, &AppDiagnostics_Instance->pending_record_event);
    if (NodeTransport_PublishServiceEvent(
            NODE_API_SERVICE_DIAGNOSTICS, NODE_API_DIAGNOSTICS_EVENT_RECORD,
            AppDiagnostics_Instance->pending_record_event.correlation_id, event_data,
            sizeof(event_data)) == NODE_API_STATUS_OK) {
        AppDiagnostics_Instance->record_event_pending = false;
    }
}
