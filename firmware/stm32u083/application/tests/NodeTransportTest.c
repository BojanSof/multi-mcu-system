#include <assert.h>
#include <string.h>
#define MULTI_MCU_RELIABILITY 1
#include "../src/NodeTransport.c"

static uint32_t Test_Time;
static bool Test_Irq;
static size_t Test_Writes;
static Protocol_Frame Test_Last;
uint32_t HAL_GetTick(void) { return Test_Time; }
void HAL_Delay(uint32_t ms) { Test_Time += ms; }
void PlatformSideband_Initialize(void) { Test_Irq = false; }
void PlatformSideband_SetReady(bool ready) { (void)ready; }
void PlatformSideband_SetIrqPending(bool pending) { Test_Irq = pending; }
bool PlatformSideband_IsEnabled(void) { return true; }
Platform_Status PlatformTransport_Initialize(void) { return PLATFORM_STATUS_OK; }
int PlatformTransport_IsEnabled(void) { return 1; }
size_t PlatformTransport_Read(uint8_t *output, size_t capacity) {
    (void)output;
    (void)capacity;
    return 0U;
}
Platform_Status PlatformTransport_Write(const uint8_t *data, size_t length) {
    assert(Protocol_DecodeFrame(data, length - 1U, &Test_Last) == PROTOCOL_STATUS_OK);
    ++Test_Writes;
    return PLATFORM_STATUS_OK;
}
void AppServices_MarkOnline(void) {}
bool AppServices_IsQuiescentActive(void) { return false; }
void AppDiagnostics_DelayResponse(void) {}
void AppDiagnostics_PrepareOutgoing(uint8_t *data, size_t *length) {
    (void)data;
    (void)length;
}
bool AppDiagnostics_IsInjectionEnabled(Diagnostics_InjectionId injection) {
    (void)injection;
    return false;
}
bool AppDiagnostics_ShouldDropAcceptance(uint16_t service, uint16_t command, uint32_t operation) {
    (void)service;
    (void)command;
    (void)operation;
    return false;
}
void AppDiagnostics_RecordOutputOrdering(uint32_t operation) { (void)operation; }

static void Test_Request(uint16_t message, uint32_t session, uint16_t sequence, uint32_t id,
                         bool tail) {
    Protocol_Frame frame = {.kind = PROTOCOL_FRAME_REQUEST, .sequence = sequence};
    NodeTransport_SetPrefix(&frame, message, session);
    if (tail) {
        NodeTransport_WriteU32(&frame.payload[frame.payload_length], id);
        frame.payload_length += 4U;
    }
    NodeTransport_HandleRequest(&frame, &NodeTransport_Instance);
}
int main(void) {
    NodeTransport_State state;
    NodeApi_Registry registry;
    NodeApi_RegistryInitialize(&registry);
    NodeTransport_Initialize(&state, 123U, &registry);
    /* New node + old host still receives original event format. */
    assert(NodeTransport_PublishServiceEvent(1U, 1U, 1U, NULL, 0U) == NODE_API_STATUS_OK);
    assert(NodeTransport_ReadU16(Test_Last.payload) == NODE_API_MESSAGE_SERVICE_EVENT);
    NodeTransport_Process(&state);
    assert(!Test_Irq);
    Test_Request(NODE_API_MESSAGE_GET_CAPABILITIES, 123U, 1U, 0U, false);
    assert((NodeTransport_ReadU32(&Test_Last.payload[6]) & NODE_API_CAPABILITY_RELIABLE_EVENTS) !=
           0U);
    Test_Request(NODE_API_MESSAGE_ENABLE_RELIABLE_EVENTS, 123U, 2U, 0U, false);
    assert(NodeTransport_ReadU32(&Test_Last.payload[6]) == 1U);
    assert(NodeTransport_PublishServiceEvent(1U, 1U, 9U, NULL, 0U) == NODE_API_STATUS_OK);
    assert(NodeTransport_PublishServiceEvent(1U, 1U, 10U, NULL, 0U) == NODE_API_STATUS_OK);
    assert(Test_Irq);
    NodeTransport_Process(&state);
    assert(Test_Last.kind == PROTOCOL_FRAME_EVENT && Test_Last.sequence == 0U);
    assert(NodeTransport_ReadU16(Test_Last.payload) == NODE_API_MESSAGE_RELIABLE_EVENT);
    assert(NodeTransport_ReadU32(&Test_Last.payload[6]) == 1U && Test_Irq);
    size_t writes = Test_Writes;
    Test_Request(NODE_API_MESSAGE_EVENT_ACK, 123U, 0U, 2U, true); /* unsent future ID */
    assert(Test_Writes == writes && NodeTransport_Instance.events.count == 2U);
    Test_Time = 250U;
    NodeTransport_Process(&state); /* lost ACK produces same event ID */
    assert(NodeTransport_ReadU32(&Test_Last.payload[6]) == 1U && Test_Irq);
    Test_Request(NODE_API_MESSAGE_EVENT_ACK, 123U, 0U, 1U, true);
    NodeTransport_Process(&state);
    assert(NodeTransport_ReadU32(&Test_Last.payload[6]) == 2U && Test_Irq);
    Test_Request(NODE_API_MESSAGE_ENABLE_RELIABLE_EVENTS, 123U, 3U, 0U, false);
    assert(NodeTransport_ReadU32(&Test_Last.payload[6]) == 2U); /* retry never clears queue */
    Test_Request(NODE_API_MESSAGE_EVENT_ACK, 999U, 0U, 2U, true);
    assert(NodeTransport_Instance.events.count == 1U);
    Test_Request(NODE_API_MESSAGE_EVENT_ACK, 123U, 0U, 2U, true);
    assert(!Test_Irq && NodeTransport_Instance.events.count == 0U);
    for (size_t i = 0U; i < NODE_EVENTS_CAPACITY; ++i) {
        assert(NodeTransport_PublishServiceEvent(1U, 1U, 9U, NULL, 0U) == NODE_API_STATUS_OK);
    }
    assert(NodeTransport_PublishServiceEvent(1U, 1U, 9U, NULL, 0U) == NODE_API_STATUS_BUSY);
    NodeApi_ServiceResponse status = {0};
    NodeTransport_GetEventStatus(&status);
    assert(status.data_length == 24U && NodeTransport_ReadU32(&status.data[16]) == 1U);
    /* Host absence exhausts sends, but explicit rediscovery re-arms delivery. */
    for (uint32_t i = 0U; i <= NODE_EVENTS_MAX_SENDS; ++i) {
        Test_Time += 250U;
        NodeTransport_Process(&state);
    }
    assert(NodeTransport_Instance.events.exhausted == 1U);
    writes = Test_Writes;
    NodeTransport_Process(&state);
    assert(Test_Writes == writes);
    Test_Request(NODE_API_MESSAGE_HELLO, 0U, 4U, 0U, false);
    assert(!NodeTransport_Instance.reliable_enabled && NodeTransport_Instance.event_negotiating);
    Test_Request(NODE_API_MESSAGE_ENABLE_RELIABLE_EVENTS, 123U, 5U, 0U, false);
    NodeTransport_Process(&state);
    assert(NodeTransport_ReadU16(Test_Last.payload) == NODE_API_MESSAGE_RELIABLE_EVENT);
    assert(NodeTransport_ReadU32(&Test_Last.payload[6]) == 3U);
    assert(NodeTransport_Instance.events.sends == 1U);
    Test_Request(NODE_API_MESSAGE_ENABLE_RELIABLE_EVENTS, 123U, 5U, 0U, false);
    assert(NodeTransport_Instance.events.sends == 1U); /* exact enable retry is idempotent */
    /* A legacy host after a reliable host gets original frames without losing data. */
    Test_Request(NODE_API_MESSAGE_HELLO, 0U, 6U, 0U, false);
    Test_Request(NODE_API_MESSAGE_LIST_SERVICES, 123U, 7U, 0U, false);
    for (size_t i = 0U; i < NODE_EVENTS_CAPACITY; ++i) {
        NodeTransport_Process(&state);
        assert(NodeTransport_ReadU16(Test_Last.payload) == NODE_API_MESSAGE_SERVICE_EVENT);
    }
    assert(NodeTransport_Instance.events.count == 0U && !Test_Irq);
    return 0;
}
