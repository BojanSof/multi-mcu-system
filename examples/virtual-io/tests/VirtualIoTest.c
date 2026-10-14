#include "virtual_io/VirtualIo.h"

#include <stdio.h>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            (void)fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #condition);    \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

typedef struct {
    uint16_t ids[8];
    uint32_t operations[8];
    uint8_t progress[8];
    size_t count;
} EventLog;

static NodeApi_Status Test_EventSink(void *context, const VirtualIo_Event *event) {
    EventLog *log = (EventLog *)context;
    if (log->count >= 8U) {
        return NODE_API_STATUS_CAPACITY_EXCEEDED;
    }
    log->ids[log->count] = event->event_id;
    log->operations[log->count] = event->operation_id;
    log->progress[log->count] = event->data[0];
    ++log->count;
    return NODE_API_STATUS_OK;
}

static int Test_PersonalitiesAndSensor(void) {
    NodeApi_Registry registry_a;
    NodeApi_Registry registry_b;
    VirtualIo_Instance a;
    VirtualIo_Instance b;
    NodeApi_ServiceResponse first;
    NodeApi_ServiceResponse repeated;
    const NodeApi_ServiceRequest sample = {VIRTUAL_IO_SENSOR_SERVICE_ID,
                                           VIRTUAL_IO_SENSOR_COMMAND_SAMPLE, 0U, NULL, 0U};

    NodeApi_RegistryInitialize(&registry_a);
    NodeApi_RegistryInitialize(&registry_b);
    VirtualIo_Initialize(&a, 1234U, NULL, NULL);
    VirtualIo_Initialize(&b, 1234U, NULL, NULL);
    CHECK(VirtualIo_RegisterServices(&a, &registry_a, VIRTUAL_IO_PERSONALITY_A) ==
          NODE_API_STATUS_OK);
    CHECK(VirtualIo_RegisterServices(&b, &registry_b, VIRTUAL_IO_PERSONALITY_B) ==
          NODE_API_STATUS_OK);
    CHECK(NodeApi_GetServiceCount(&registry_a) == 1U);
    CHECK(NodeApi_GetServiceCount(&registry_b) == 2U);
    CHECK(NodeApi_DispatchRequest(&registry_a, &sample, &first) == NODE_API_STATUS_OK);
    CHECK(NodeApi_DispatchRequest(&registry_b, &sample, &repeated) == NODE_API_STATUS_OK);
    CHECK(first.data_length == VIRTUAL_IO_SENSOR_SAMPLE_RESPONSE_SIZE);
    CHECK(repeated.data_length == first.data_length);
    for (size_t index = 0U; index < first.data_length; ++index) {
        CHECK(first.data[index] == repeated.data[index]);
    }
    return 0;
}

static int Test_ActuatorIdempotencyAndEvents(void) {
    NodeApi_Registry registry;
    VirtualIo_Instance instance;
    EventLog events = {{0U}, {0U}, {0U}, 0U};
    NodeApi_ServiceResponse response;
    const uint8_t duration[4] = {0xE8U, 0x03U, 0x00U, 0x00U};
    const NodeApi_ServiceRequest start = {VIRTUAL_IO_ACTUATOR_SERVICE_ID,
                                          VIRTUAL_IO_ACTUATOR_COMMAND_START, 42U, duration,
                                          sizeof(duration)};
    const NodeApi_ServiceRequest other = {VIRTUAL_IO_ACTUATOR_SERVICE_ID,
                                          VIRTUAL_IO_ACTUATOR_COMMAND_START, 43U, duration,
                                          sizeof(duration)};
    const NodeApi_ServiceRequest unsupported = {VIRTUAL_IO_ACTUATOR_SERVICE_ID, 99U, 44U, NULL, 0U};

    NodeApi_RegistryInitialize(&registry);
    VirtualIo_Initialize(&instance, 7U, Test_EventSink, &events);
    CHECK(VirtualIo_RegisterServices(&instance, &registry, VIRTUAL_IO_PERSONALITY_B) ==
          NODE_API_STATUS_OK);
    CHECK(NodeApi_DispatchRequest(&registry, &unsupported, &response) ==
          NODE_API_STATUS_UNSUPPORTED_COMMAND);
    CHECK(NodeApi_DispatchRequest(&registry, &start, &response) == NODE_API_STATUS_OK);
    CHECK(VirtualIo_GetExecutionCount(&instance) == 1U);
    CHECK(NodeApi_DispatchRequest(&registry, &start, &response) == NODE_API_STATUS_OK);
    CHECK(VirtualIo_GetExecutionCount(&instance) == 1U);
    CHECK(NodeApi_DispatchRequest(&registry, &other, &response) == NODE_API_STATUS_BUSY);
    CHECK(VirtualIo_Tick(&instance, 100U) == NODE_API_STATUS_OK);
    CHECK(VirtualIo_Tick(&instance, 1100U) == NODE_API_STATUS_OK);
    CHECK(events.count == 4U);
    CHECK(events.ids[0] == VIRTUAL_IO_ACTUATOR_EVENT_PROGRESS && events.progress[0] == 25U);
    CHECK(events.ids[1] == VIRTUAL_IO_ACTUATOR_EVENT_PROGRESS && events.progress[1] == 50U);
    CHECK(events.ids[2] == VIRTUAL_IO_ACTUATOR_EVENT_PROGRESS && events.progress[2] == 75U);
    CHECK(events.ids[3] == VIRTUAL_IO_ACTUATOR_EVENT_COMPLETE);
    for (size_t index = 0U; index < events.count; ++index) {
        CHECK(events.operations[index] == 42U);
    }
    CHECK(NodeApi_DispatchRequest(&registry, &other, &response) == NODE_API_STATUS_OK);
    CHECK(VirtualIo_GetExecutionCount(&instance) == 2U);
    CHECK(NodeApi_DispatchRequest(&registry, &start, &response) == NODE_API_STATUS_OK);
    CHECK(VirtualIo_GetExecutionCount(&instance) == 2U);
    return 0;
}

static int Test_OutputOrderingRegression(void) {
    NodeApi_Registry fixed_registry;
    NodeApi_Registry buggy_registry;
    VirtualIo_Instance fixed;
    VirtualIo_Instance buggy;
    VirtualIo_OutputState fixed_state;
    VirtualIo_OutputState buggy_state;
    VirtualIo_OutputState duplicate_state;
    NodeApi_ServiceResponse response;
    const uint8_t duration[4] = {0xE8U, 0x03U, 0x00U, 0x00U};
    const NodeApi_ServiceRequest fixed_start = {VIRTUAL_IO_ACTUATOR_SERVICE_ID,
                                                VIRTUAL_IO_ACTUATOR_COMMAND_START, 100U, duration,
                                                sizeof(duration)};
    const NodeApi_ServiceRequest buggy_start = {VIRTUAL_IO_ACTUATOR_SERVICE_ID,
                                                VIRTUAL_IO_ACTUATOR_COMMAND_START, 200U, duration,
                                                sizeof(duration)};

    NodeApi_RegistryInitialize(&fixed_registry);
    VirtualIo_Initialize(&fixed, 1U, NULL, NULL);
    CHECK(VirtualIo_RegisterServices(&fixed, &fixed_registry, VIRTUAL_IO_PERSONALITY_B) ==
          NODE_API_STATUS_OK);
    VirtualIo_SetEarlyOutputInjection(&fixed, false);
    CHECK(NodeApi_DispatchRequest(&fixed_registry, &fixed_start, &response) == NODE_API_STATUS_OK);
    CHECK(VirtualIo_GetOutputState(&fixed, &fixed_state) == NODE_API_STATUS_OK);
    CHECK(fixed_state.prerequisite_ready && fixed_state.output_enabled);
    CHECK(fixed_state.prerequisite_order < fixed_state.output_enable_order);
    CHECK(fixed_state.ordering_violation_count == 0U);
    CHECK(fixed_state.last_operation_id == 100U);

    /* A retry is acceptance replay, not a second modeled output transition. */
    CHECK(NodeApi_DispatchRequest(&fixed_registry, &fixed_start, &response) == NODE_API_STATUS_OK);
    CHECK(VirtualIo_GetOutputState(&fixed, &duplicate_state) == NODE_API_STATUS_OK);
    CHECK(duplicate_state.transition_count == fixed_state.transition_count);
    CHECK(duplicate_state.ordering_violation_count == 0U);

    NodeApi_RegistryInitialize(&buggy_registry);
    VirtualIo_Initialize(&buggy, 1U, NULL, NULL);
    CHECK(VirtualIo_RegisterServices(&buggy, &buggy_registry, VIRTUAL_IO_PERSONALITY_B) ==
          NODE_API_STATUS_OK);
    VirtualIo_SetEarlyOutputInjection(&buggy, true);
    CHECK(NodeApi_DispatchRequest(&buggy_registry, &buggy_start, &response) == NODE_API_STATUS_OK);
    CHECK(VirtualIo_GetOutputState(&buggy, &buggy_state) == NODE_API_STATUS_OK);
    CHECK(buggy_state.early_output_injection_enabled);
    CHECK(buggy_state.prerequisite_ready && buggy_state.output_enabled);
    CHECK(buggy_state.output_enable_order < buggy_state.prerequisite_order);
    CHECK(buggy_state.ordering_violation_count == 1U);
    CHECK(buggy_state.last_operation_id == 200U);
    return 0;
}

int main(void) {
    CHECK(Test_PersonalitiesAndSensor() == 0);
    CHECK(Test_ActuatorIdempotencyAndEvents() == 0);
    CHECK(Test_OutputOrderingRegression() == 0);
    return 0;
}
