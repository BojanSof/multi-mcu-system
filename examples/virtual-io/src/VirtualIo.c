#include "virtual_io/VirtualIo.h"

#include <string.h>

static void VirtualIo_WriteU32(uint8_t *destination, uint32_t value) {
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8U);
    destination[2] = (uint8_t)(value >> 16U);
    destination[3] = (uint8_t)(value >> 24U);
}

static uint32_t VirtualIo_ReadU32(const uint8_t *source) {
    return (uint32_t)source[0] | ((uint32_t)source[1] << 8U) | ((uint32_t)source[2] << 16U) |
           ((uint32_t)source[3] << 24U);
}

static NodeApi_Status VirtualIo_SensorHandler(void *context, const NodeApi_ServiceRequest *request,
                                              NodeApi_ServiceResponse *response) {
    VirtualIo_Instance *instance = (VirtualIo_Instance *)context;
    uint32_t mixed;
    int32_t sample;

    if (request->command_id != VIRTUAL_IO_SENSOR_COMMAND_SAMPLE) {
        return NODE_API_STATUS_UNSUPPORTED_COMMAND;
    }
    if (request->data_length != 0U) {
        return NODE_API_STATUS_INVALID_ARGUMENT;
    }
    /* A deterministic integer-only signal; it is not a physical measurement. */
    mixed = instance->seed + (instance->sample_index * 1664525U) + 1013904223U;
    sample = (int32_t)(mixed % 2001U) - 1000;
    VirtualIo_WriteU32(&response->data[0], (uint32_t)sample);
    VirtualIo_WriteU32(&response->data[4], instance->sample_index);
    response->data_length = VIRTUAL_IO_SENSOR_SAMPLE_RESPONSE_SIZE;
    ++instance->sample_index;
    return NODE_API_STATUS_OK;
}

static bool VirtualIo_FindOperation(const VirtualIo_Instance *instance, uint32_t operation_id,
                                    uint32_t *execution) {
    size_t index;
    for (index = 0U; index < instance->operation_history_count; ++index) {
        if (instance->operation_history[index] == operation_id) {
            *execution = instance->operation_execution[index];
            return true;
        }
    }
    return false;
}

static void VirtualIo_RememberOperation(VirtualIo_Instance *instance, uint32_t operation_id,
                                        uint32_t execution) {
    const size_t index = instance->operation_history_next;
    instance->operation_history[index] = operation_id;
    instance->operation_execution[index] = execution;
    instance->operation_history_next = (index + 1U) % VIRTUAL_IO_OPERATION_HISTORY_CAPACITY;
    if (instance->operation_history_count < VIRTUAL_IO_OPERATION_HISTORY_CAPACITY) {
        ++instance->operation_history_count;
    }
}

static NodeApi_Status VirtualIo_ActuatorHandler(void *context,
                                                const NodeApi_ServiceRequest *request,
                                                NodeApi_ServiceResponse *response) {
    VirtualIo_Instance *instance = (VirtualIo_Instance *)context;
    uint32_t duration_ms;
    uint32_t execution;

    if (request->command_id != VIRTUAL_IO_ACTUATOR_COMMAND_START) {
        return NODE_API_STATUS_UNSUPPORTED_COMMAND;
    }
    if ((request->operation_id == 0U) ||
        (request->data_length != VIRTUAL_IO_ACTUATOR_START_REQUEST_SIZE)) {
        return NODE_API_STATUS_INVALID_ARGUMENT;
    }
    if (VirtualIo_FindOperation(instance, request->operation_id, &execution)) {
        VirtualIo_WriteU32(response->data, execution);
        response->data_length = VIRTUAL_IO_ACTUATOR_START_RESPONSE_SIZE;
        return NODE_API_STATUS_OK;
    }
    if (instance->active) {
        return NODE_API_STATUS_BUSY;
    }
    duration_ms = VirtualIo_ReadU32(request->data);
    if ((duration_ms < VIRTUAL_IO_ACTUATOR_MIN_DURATION_MS) ||
        (duration_ms > VIRTUAL_IO_ACTUATOR_MAX_DURATION_MS)) {
        return NODE_API_STATUS_INVALID_ARGUMENT;
    }
    ++instance->execution_count;
    instance->active = true;
    instance->active_operation_id = request->operation_id;
    instance->active_duration_ms = duration_ms;
    instance->active_started_ms = 0U;
    instance->next_progress_step = 1U;
    instance->start_time_initialized = false;
    VirtualIo_RememberOperation(instance, request->operation_id, instance->execution_count);
    VirtualIo_WriteU32(response->data, instance->execution_count);
    response->data_length = VIRTUAL_IO_ACTUATOR_START_RESPONSE_SIZE;
    return NODE_API_STATUS_OK;
}

void VirtualIo_Initialize(VirtualIo_Instance *instance, uint32_t seed,
                          VirtualIo_EventSink event_sink, void *event_context) {
    if (instance != NULL) {
        (void)memset(instance, 0, sizeof(*instance));
        instance->seed = seed;
        instance->event_sink = event_sink;
        instance->event_context = event_context;
    }
}

NodeApi_Status VirtualIo_RegisterServices(VirtualIo_Instance *instance, NodeApi_Registry *registry,
                                          VirtualIo_Personality personality) {
    NodeApi_ServiceRegistration registration;
    NodeApi_Status status;

    if ((instance == NULL) || (registry == NULL) ||
        ((personality != VIRTUAL_IO_PERSONALITY_A) && (personality != VIRTUAL_IO_PERSONALITY_B))) {
        return NODE_API_STATUS_INVALID_ARGUMENT;
    }
    registration.descriptor.service_id = VIRTUAL_IO_SENSOR_SERVICE_ID;
    registration.descriptor.version_major = VIRTUAL_IO_VERSION_MAJOR;
    registration.descriptor.version_minor = VIRTUAL_IO_VERSION_MINOR;
    registration.descriptor.flags = 0U;
    registration.handler = VirtualIo_SensorHandler;
    registration.context = instance;
    status = NodeApi_RegisterService(registry, &registration);
    if ((status != NODE_API_STATUS_OK) || (personality == VIRTUAL_IO_PERSONALITY_A)) {
        return status;
    }
    registration.descriptor.service_id = VIRTUAL_IO_ACTUATOR_SERVICE_ID;
    registration.handler = VirtualIo_ActuatorHandler;
    return NodeApi_RegisterService(registry, &registration);
}

static NodeApi_Status VirtualIo_Publish(VirtualIo_Instance *instance, uint16_t event_id,
                                        uint8_t progress, uint32_t execution) {
    VirtualIo_Event event;
    if (instance->event_sink == NULL) {
        return NODE_API_STATUS_OK;
    }
    (void)memset(&event, 0, sizeof(event));
    event.service_id = VIRTUAL_IO_ACTUATOR_SERVICE_ID;
    event.event_id = event_id;
    event.operation_id = instance->active_operation_id;
    if (event_id == VIRTUAL_IO_ACTUATOR_EVENT_PROGRESS) {
        event.data[0] = progress;
        VirtualIo_WriteU32(&event.data[1], execution);
        event.data_length = VIRTUAL_IO_ACTUATOR_PROGRESS_EVENT_SIZE;
    } else {
        VirtualIo_WriteU32(event.data, execution);
        event.data_length = VIRTUAL_IO_ACTUATOR_COMPLETE_EVENT_SIZE;
    }
    return instance->event_sink(instance->event_context, &event);
}

NodeApi_Status VirtualIo_Tick(VirtualIo_Instance *instance, uint32_t now_ms) {
    static const uint8_t progress_percent[] = {25U, 50U, 75U};
    uint32_t elapsed;
    NodeApi_Status status;

    if (instance == NULL) {
        return NODE_API_STATUS_INVALID_ARGUMENT;
    }
    if (!instance->active) {
        return NODE_API_STATUS_OK;
    }
    if (!instance->start_time_initialized) {
        instance->active_started_ms = now_ms;
        instance->start_time_initialized = true;
        return NODE_API_STATUS_OK;
    }
    elapsed = now_ms - instance->active_started_ms;
    while (instance->next_progress_step <= 3U) {
        const uint32_t threshold =
            (instance->active_duration_ms * instance->next_progress_step) / 4U;
        if (elapsed < threshold) {
            break;
        }
        status = VirtualIo_Publish(instance, VIRTUAL_IO_ACTUATOR_EVENT_PROGRESS,
                                   progress_percent[instance->next_progress_step - 1U],
                                   instance->execution_count);
        if (status != NODE_API_STATUS_OK) {
            return status;
        }
        ++instance->next_progress_step;
    }
    if ((elapsed >= instance->active_duration_ms) && (instance->next_progress_step == 4U)) {
        status = VirtualIo_Publish(instance, VIRTUAL_IO_ACTUATOR_EVENT_COMPLETE, 100U,
                                   instance->execution_count);
        if (status != NODE_API_STATUS_OK) {
            return status;
        }
        instance->active = false;
    }
    return NODE_API_STATUS_OK;
}

uint32_t VirtualIo_GetExecutionCount(const VirtualIo_Instance *instance) {
    return (instance != NULL) ? instance->execution_count : 0U;
}
