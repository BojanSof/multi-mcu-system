#ifndef VIRTUAL_IO_VIRTUAL_IO_H
#define VIRTUAL_IO_VIRTUAL_IO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "node_api/NodeServices.h"

#define VIRTUAL_IO_VERSION_MAJOR 1U
#define VIRTUAL_IO_VERSION_MINOR 0U
#define VIRTUAL_IO_OPERATION_HISTORY_CAPACITY 8U

#define VIRTUAL_IO_SENSOR_SERVICE_ID 0x1001U
#define VIRTUAL_IO_ACTUATOR_SERVICE_ID 0x1002U

#define VIRTUAL_IO_SENSOR_COMMAND_SAMPLE 0x0001U
#define VIRTUAL_IO_ACTUATOR_COMMAND_START 0x0001U
#define VIRTUAL_IO_ACTUATOR_EVENT_PROGRESS 0x0001U
#define VIRTUAL_IO_ACTUATOR_EVENT_COMPLETE 0x0002U

#define VIRTUAL_IO_SENSOR_SAMPLE_RESPONSE_SIZE 8U
#define VIRTUAL_IO_ACTUATOR_START_REQUEST_SIZE 4U
#define VIRTUAL_IO_ACTUATOR_START_RESPONSE_SIZE 4U
#define VIRTUAL_IO_ACTUATOR_PROGRESS_EVENT_SIZE 5U
#define VIRTUAL_IO_ACTUATOR_COMPLETE_EVENT_SIZE 4U

#define VIRTUAL_IO_ACTUATOR_MIN_DURATION_MS 100U
#define VIRTUAL_IO_ACTUATOR_MAX_DURATION_MS 60000U

typedef enum { VIRTUAL_IO_PERSONALITY_A = 0, VIRTUAL_IO_PERSONALITY_B = 1 } VirtualIo_Personality;

typedef struct {
    uint16_t service_id;
    uint16_t event_id;
    uint32_t operation_id;
    uint8_t data[NODE_API_SERVICE_DATA_MAX_LENGTH];
    size_t data_length;
} VirtualIo_Event;

typedef NodeApi_Status (*VirtualIo_EventSink)(void *context, const VirtualIo_Event *event);

/*
 * Software-only ordering state. These flags model a prerequisite and output;
 * they do not describe, sense, or control an electrical signal.
 */
typedef struct {
    bool early_output_injection_enabled;
    bool prerequisite_ready;
    bool output_enabled;
    uint32_t transition_count;
    uint32_t prerequisite_order;
    uint32_t output_enable_order;
    uint32_t ordering_violation_count;
    uint32_t last_operation_id;
} VirtualIo_OutputState;

typedef struct {
    uint32_t seed;
    uint32_t sample_index;
    uint32_t execution_count;
    uint32_t active_operation_id;
    uint32_t active_started_ms;
    uint32_t active_duration_ms;
    uint8_t next_progress_step;
    bool active;
    bool start_time_initialized;
    VirtualIo_OutputState output_state;
    uint32_t operation_history[VIRTUAL_IO_OPERATION_HISTORY_CAPACITY];
    uint32_t operation_execution[VIRTUAL_IO_OPERATION_HISTORY_CAPACITY];
    size_t operation_history_count;
    size_t operation_history_next;
    VirtualIo_EventSink event_sink;
    void *event_context;
} VirtualIo_Instance;

void VirtualIo_Initialize(VirtualIo_Instance *instance, uint32_t seed,
                          VirtualIo_EventSink event_sink, void *event_context);
NodeApi_Status VirtualIo_RegisterServices(VirtualIo_Instance *instance, NodeApi_Registry *registry,
                                          VirtualIo_Personality personality);
NodeApi_Status VirtualIo_Tick(VirtualIo_Instance *instance, uint32_t now_ms);
uint32_t VirtualIo_GetExecutionCount(const VirtualIo_Instance *instance);
void VirtualIo_SetEarlyOutputInjection(VirtualIo_Instance *instance, bool enabled);
NodeApi_Status VirtualIo_GetOutputState(const VirtualIo_Instance *instance,
                                        VirtualIo_OutputState *state);

#endif
