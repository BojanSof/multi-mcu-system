#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#define MULTI_MCU_RELIABILITY 1
int64_t HostMetrics_TestClock;
#include "../../src/node_manager/HostNodeManager.c"
static size_t Test_Events;
static size_t Test_Acks;
static uint32_t Test_Ack;
static bool Test_Full;
bool HostTransport_IsAvailable(void) { return true; }
int HostTransport_Write(const uint8_t *data, size_t length) {
    if (Test_Full) {
        return -ENOSPC;
    }
    Protocol_Frame frame;
    assert(length > 0);
    assert(Protocol_DecodeFrame(data, length - 1U, &frame) == PROTOCOL_STATUS_OK);
    if (HostNodeManager_ReadU16(frame.payload) == NODE_API_MESSAGE_EVENT_ACK) {
        assert(frame.sequence == 0U);
        Test_Acks++;
        Test_Ack = HostNodeManager_ReadU32(&frame.payload[6]);
    }
    return 0;
}
size_t HostTransport_Read(uint8_t *data, size_t capacity) {
    (void)data;
    (void)capacity;
    return 0;
}
bool HostSignals_NodeReady(void) { return true; }
bool HostSignals_NodeIrqAsserted(void) { return false; }
int HostSignals_SetDebug(bool active) {
    (void)active;
    return 0;
}
static void Test_Record(HostNodeManager_RecordKind kind, uint32_t session, uint32_t operation,
                        uint16_t service, uint16_t code, uint32_t status, const uint8_t *data,
                        size_t length) {
    (void)operation;
    (void)code;
    (void)status;
    (void)data;
    assert(kind == HOST_NODE_RECORD_EVENT && session == 11U && service == 0x1002 && length == 4U);
    Test_Events++;
}
int main(void) {
    HostNodeManager_Initialize();
    HostNodeManager.status.session_generation = 11U;
    Protocol_Frame enable = {.kind = PROTOCOL_FRAME_RESPONSE, .payload_length = 10U};
    HostNodeManager_WriteU32(&enable.payload[2], 11U);
    HostNodeManager_WriteU32(&enable.payload[6], 1U);
    HostNodeManager_Advance(NODE_API_MESSAGE_ENABLE_RELIABLE_EVENTS, &enable);
    assert(HostNodeManager.reliable_events && HostNodeManager.last_event_id == 0U);
    HostNodeManager_SetRecordCallback(Test_Record);
    Protocol_Frame event = {.kind = PROTOCOL_FRAME_EVENT, .payload_length = 22U};
    HostNodeManager_WriteU16(event.payload, NODE_API_MESSAGE_RELIABLE_EVENT);
    HostNodeManager_WriteU32(&event.payload[2], 11U);
    HostNodeManager_WriteU32(&event.payload[6], 1U);
    HostNodeManager_WriteU16(&event.payload[10], 0x1002);
    HostNodeManager_WriteU16(&event.payload[12], 2U);
    HostNodeManager_WriteU32(&event.payload[14], 123U);
    HostNodeManager_FrameReceived(&event, NULL);
    assert(Test_Events == 1 && Test_Ack == 1U && Test_Acks == 1);
    HostNodeManager_FrameReceived(&event, NULL);
    assert(Test_Events == 1 && Test_Acks == 2);
    HostNodeManager_WriteU32(&event.payload[6], 3U);
    HostNodeManager_FrameReceived(&event, NULL);
    assert(Test_Events == 1 && Test_Acks == 2);
    HostNodeManager_WriteU32(&event.payload[6], 2U);
    HostNodeManager_WriteU32(&event.payload[2], 99U);
    HostNodeManager_FrameReceived(&event, NULL);
    assert(Test_Events == 1 && HostNodeManager.status.session_generation == 11U);
    HostNodeManager_WriteU32(&event.payload[2], 11U);
    event.payload_length = 17U;
    HostNodeManager_FrameReceived(&event, NULL);
    assert(Test_Events == 1);
    event.payload_length = 22U;
    Test_Full = true;
    HostNodeManager_FrameReceived(&event, NULL);
    assert(Test_Events == 2 && HostNodeManager.event_ack_pending);
    Test_Full = false;
    HostNodeManager_SendEventAck();
    assert(!HostNodeManager.event_ack_pending && Test_Ack == 2U);
    HostNodeManager_FrameReceived(&event, NULL);
    assert(Test_Events == 2);
    /* Discovery restart after a lost ACK must not republish admitted events. */
    HostNodeManager_Initialize();
    HostNodeManager.status.session_generation = 11U;
    HostNodeManager_WriteU32(&enable.payload[6], 2U);
    HostNodeManager_Advance(NODE_API_MESSAGE_ENABLE_RELIABLE_EVENTS, &enable);
    assert(HostNodeManager.last_event_id == 2U);
    HostNodeManager_FrameReceived(&event, NULL);
    assert(Test_Events == 2 && Test_Ack == 2U);
    HostNodeManager_SetRecordCallback(NULL);
    HostNodeManager_WriteU32(&event.payload[6], 3U);
    const size_t acks = Test_Acks;
    HostNodeManager_FrameReceived(&event, NULL);
    assert(Test_Events == 2 && Test_Acks == acks);
    HostNodeManager.reliable_events = false;
    HostNodeManager_FrameReceived(&event, NULL);
    assert(Test_Events == 2);
    HostNodeManager_CompleteRequest();
    HostNodeManager.status.state = HOST_LINK_ONLINE;
    HostNodeManager.status.services[0] = (NodeApi_ServiceDescriptor){
        .service_id = NODE_API_SERVICE_LIFECYCLE, .version_major = 1U, .version_minor = 0U};
    HostNodeManager.status.service_count = 1U;
    assert(HostNodeManager_CallServiceWithOperation(NODE_API_SERVICE_LIFECYCLE,
                                                    NODE_API_LIFECYCLE_COMMAND_ENTER_MAINTENANCE,
                                                    NULL, 0U, 44U) == 0);
    assert(HostNodeManager_RecoverySuppressed());
    HostNodeManager_ClearPending();
    /* A lost response must not turn deliberate maintenance into auto-reset. */
    assert(HostNodeManager_RecoverySuppressed());
    assert(HostNodeManager_CallServiceWithOperation(NODE_API_SERVICE_LIFECYCLE,
                                                    NODE_API_LIFECYCLE_COMMAND_EXIT_MAINTENANCE,
                                                    NULL, 0U, 45U) == 0);
    assert(!HostNodeManager_RecoverySuppressed());
    /* Valid additive lifecycle replies must not become protocol errors. */
    const uint16_t commands[] = {NODE_API_LIFECYCLE_COMMAND_READ_FAULT_HISTORY,
                                 NODE_API_LIFECYCLE_COMMAND_CLEAR_FAULT_HISTORY,
                                 NODE_API_LIFECYCLE_COMMAND_GET_EVENT_STATUS};
    const uint16_t lengths[] = {60U, 12U, 24U};
    for (size_t i = 0U; i < 3U; ++i) {
        const uint32_t errors = HostNodeManager_Lifetime.protocol_errors;
        HostNodeManager.pending_message = NODE_API_MESSAGE_SERVICE_CALL;
        HostNodeManager.pending_service = NODE_API_SERVICE_LIFECYCLE;
        HostNodeManager.pending_command = commands[i];
        HostNodeManager.pending_operation = 99U;
        Protocol_Frame reply = {.kind = PROTOCOL_FRAME_RESPONSE,
                                .payload_length = (uint16_t)(14U + lengths[i])};
        HostNodeManager_WriteU32(&reply.payload[2], 11U);
        HostNodeManager_WriteU16(&reply.payload[6], NODE_API_SERVICE_LIFECYCLE);
        HostNodeManager_WriteU16(&reply.payload[8], commands[i]);
        HostNodeManager_WriteU32(&reply.payload[10], 99U);
        HostNodeManager_Advance(NODE_API_MESSAGE_SERVICE_CALL, &reply);
        assert(HostNodeManager_Lifetime.protocol_errors == errors);
        assert(HostNodeManager.pending_message == 0U);
    }
    return 0;
}
