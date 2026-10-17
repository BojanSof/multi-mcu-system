#include "AppLifecycle.h"

#include <limits.h>
#include <string.h>

#include "protocol/ProtocolTypes.h"
#include "stm32u083_platform/PlatformSideband.h"
#include "stm32u083_platform/PlatformSupervision.h"

#define APP_LIFECYCLE_VERSION_MAJOR 0U
#define APP_LIFECYCLE_VERSION_MINOR 6U
#define APP_LIFECYCLE_VERSION_PATCH 0U
#define APP_LIFECYCLE_BUILD_IDENTIFIER 0x00060001UL

static void AppLifecycle_WriteU16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
}

static void AppLifecycle_WriteU32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
    bytes[2] = (uint8_t)(value >> 16U);
    bytes[3] = (uint8_t)(value >> 24U);
}

static void AppLifecycle_WriteStatus(const AppLifecycle_State *state,
                                     NodeApi_ServiceResponse *response) {
    response->data[0] = (uint8_t)state->state;
    response->data[1] = (uint8_t)state->reset_reason;
    AppLifecycle_WriteU16(&response->data[2], state->raw_reset_flags);
    AppLifecycle_WriteU32(&response->data[4], state->boot_count);
    AppLifecycle_WriteU32(&response->data[8], state->progress_count);
    response->data_length = NODE_API_LIFECYCLE_STATUS_SIZE;
}

static void AppLifecycle_WriteVersions(NodeApi_ServiceResponse *response) {
    AppLifecycle_WriteU16(&response->data[0], APP_LIFECYCLE_VERSION_MAJOR);
    AppLifecycle_WriteU16(&response->data[2], APP_LIFECYCLE_VERSION_MINOR);
    AppLifecycle_WriteU16(&response->data[4], APP_LIFECYCLE_VERSION_PATCH);
    response->data[6] = PROTOCOL_WIRE_VERSION_MAJOR;
    response->data[7] = 0U;
    AppLifecycle_WriteU16(&response->data[8], NODE_API_MANAGEMENT_VERSION_MAJOR);
    AppLifecycle_WriteU16(&response->data[10], NODE_API_MANAGEMENT_VERSION_MINOR);
    AppLifecycle_WriteU32(&response->data[12], APP_LIFECYCLE_BUILD_IDENTIFIER);
    response->data_length = NODE_API_LIFECYCLE_VERSIONS_SIZE;
}

static NodeApi_Status AppLifecycle_Handler(void *context, const NodeApi_ServiceRequest *request,
                                           NodeApi_ServiceResponse *response) {
    AppLifecycle_State *state = context;
    if ((state == NULL) || (request == NULL) || (response == NULL)) {
        return NODE_API_STATUS_INVALID_ARGUMENT;
    }
    if (request->data_length != 0U) {
        return NODE_API_STATUS_INVALID_ARGUMENT;
    }
    switch (request->command_id) {
    case NODE_API_LIFECYCLE_COMMAND_GET_STATUS:
        AppLifecycle_WriteStatus(state, response);
        return NODE_API_STATUS_OK;
    case NODE_API_LIFECYCLE_COMMAND_ENTER_MAINTENANCE:
        state->state = NODE_API_LIFECYCLE_MAINTENANCE;
        state->ready_transition_polls = 2U;
        AppLifecycle_WriteStatus(state, response);
        return NODE_API_STATUS_OK;
    case NODE_API_LIFECYCLE_COMMAND_EXIT_MAINTENANCE:
        state->state = NODE_API_LIFECYCLE_ONLINE;
        state->ready_transition_polls = 2U;
        AppLifecycle_WriteStatus(state, response);
        return NODE_API_STATUS_OK;
    case NODE_API_LIFECYCLE_COMMAND_GET_VERSIONS:
        AppLifecycle_WriteVersions(response);
        return NODE_API_STATUS_OK;
    case NODE_API_LIFECYCLE_COMMAND_ENTER_SHUTDOWN:
        state->state = NODE_API_LIFECYCLE_SHUTDOWN;
        state->ready_transition_polls = 2U;
        AppLifecycle_WriteStatus(state, response);
        return NODE_API_STATUS_OK;
    case NODE_API_LIFECYCLE_COMMAND_EXIT_SHUTDOWN:
        state->state = NODE_API_LIFECYCLE_ONLINE;
        state->ready_transition_polls = 2U;
        AppLifecycle_WriteStatus(state, response);
        return NODE_API_STATUS_OK;
    default:
        return NODE_API_STATUS_UNSUPPORTED_COMMAND;
    }
}

NodeApi_Status AppLifecycle_Initialize(AppLifecycle_State *state, NodeApi_Registry *registry) {
    Platform_ResetInfo reset_info;
    NodeApi_ServiceRegistration service;
    if ((state == NULL) || (registry == NULL)) {
        return NODE_API_STATUS_INVALID_ARGUMENT;
    }
    reset_info = PlatformSupervision_GetResetInfo();
    (void)memset(state, 0, sizeof(*state));
    state->state = NODE_API_LIFECYCLE_INIT;
    state->reset_reason = (NodeApi_ResetReason)reset_info.reason;
    state->raw_reset_flags = reset_info.raw_flags;
    state->boot_count = reset_info.boot_count;
    service = (NodeApi_ServiceRegistration){
        .descriptor = {.service_id = NODE_API_SERVICE_LIFECYCLE,
                       .version_major = NODE_API_LIFECYCLE_VERSION_MAJOR,
                       .version_minor = NODE_API_LIFECYCLE_VERSION_MINOR,
                       .flags = 0U},
        .handler = AppLifecycle_Handler,
        .context = state,
    };
    return NodeApi_RegisterService(registry, &service);
}

void AppLifecycle_RecordProgress(AppLifecycle_State *state) {
    if ((state != NULL) && (state->progress_count != UINT32_MAX)) {
        ++state->progress_count;
    }
}

bool AppLifecycle_IsMaintenance(const AppLifecycle_State *state) {
    return (state != NULL) && (state->state == NODE_API_LIFECYCLE_MAINTENANCE);
}

bool AppLifecycle_IsQuiescent(const AppLifecycle_State *state) {
    return (state != NULL) && ((state->state == NODE_API_LIFECYCLE_MAINTENANCE) ||
                               (state->state == NODE_API_LIFECYCLE_SHUTDOWN));
}

void AppLifecycle_MarkReady(AppLifecycle_State *state) {
    if ((state != NULL) && (state->state == NODE_API_LIFECYCLE_INIT)) {
        state->state = NODE_API_LIFECYCLE_READY;
    }
}

void AppLifecycle_MarkOnline(AppLifecycle_State *state) {
    if ((state != NULL) && (state->state == NODE_API_LIFECYCLE_READY)) {
        state->state = NODE_API_LIFECYCLE_ONLINE;
    }
}

void AppLifecycle_ApplyReadyState(AppLifecycle_State *state) {
    if ((state == NULL) || (state->ready_transition_polls == 0U)) {
        return;
    }
    if (state->ready_transition_polls == 2U) {
        PlatformSideband_SetReady(false);
    } else {
        /* Transport and lifecycle control remain available in maintenance. */
        PlatformSideband_SetReady(true);
    }
    --state->ready_transition_polls;
}
