#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
int64_t HostMetrics_TestClock;
#include "../../src/node_manager/HostNodeManager.c"

static bool Test_Ready = true;
static int Test_WriteError;
bool HostTransport_IsAvailable(void) { return true; }
int HostTransport_Write(const uint8_t *data, size_t length) {
    (void)data;
    (void)length;
    return Test_WriteError;
}
size_t HostTransport_Read(uint8_t *data, size_t capacity) {
    (void)data;
    (void)capacity;
    return 0;
}
bool HostSignals_NodeReady(void) { return Test_Ready; }
bool HostSignals_NodeIrqAsserted(void) { return false; }
int HostSignals_SetDebug(bool active) {
    (void)active;
    return 0;
}

int main(void) {
    HostMetrics_TestClock = 10;
    HostNodeManager_Initialize();
    assert(HostNodeManager_GetMetrics().requests == 1);
    Protocol_Frame response = {.kind = PROTOCOL_FRAME_RESPONSE,
                               .sequence = (uint16_t)(HostNodeManager.sequence + 1),
                               .payload_length = NODE_API_HELLO_RESPONSE_SIZE};
    HostNodeManager_WriteU16(response.payload, NODE_API_MESSAGE_HELLO);
    HostNodeManager_WriteU32(response.payload + 2, 12);
    HostNodeManager_WriteU16(response.payload + 6, NODE_API_MANAGEMENT_VERSION_MAJOR);
    response.payload[10] = PROTOCOL_WIRE_VERSION_MAJOR;
    HostNodeManager_FrameReceived(&response, NULL);
    assert(HostNodeManager_GetMetrics().responses == 0);
    response.sequence = HostNodeManager.sequence;
    HostMetrics_TestClock = 20;
    HostNodeManager_FrameReceived(&response, NULL);
    assert(HostNodeManager_GetMetrics().responses == 1);
    assert(HostNodeManager_GetMetrics().latency_last_ms == 10);
    assert(HostNodeManager_GetMetrics().requests == 2);
    HostMetrics_TestClock = 520;
    HostNodeManager_Process();
    assert(HostNodeManager_GetMetrics().retries == 1);
    assert(HostNodeManager_GetMetrics().timeouts == 1);
    response.sequence = HostNodeManager.sequence;
    response.payload_length = NODE_API_REQUEST_PREFIX_SIZE + 2;
    HostNodeManager_WriteU16(response.payload, NODE_API_MESSAGE_GET_IDENTITY);
    response.payload[6] = 1;
    response.payload[7] = 'x';
    HostMetrics_TestClock = 531;
    HostNodeManager_FrameReceived(&response, NULL);
    HostNodeManager_Metrics metrics = HostNodeManager_GetMetrics();
    assert(metrics.responses == 2 && metrics.requests == 3);
    assert(metrics.latency_count == 2 && metrics.latency_sum_ms == 521);
    assert(metrics.latency_max_ms == 511);
    HostNodeManager_FrameError(PROTOCOL_STATUS_BAD_CRC, NULL);
    assert(HostNodeManager_GetMetrics().protocol_errors == 1);
    HostNodeManager_Initialize();
    metrics = HostNodeManager_GetMetrics();
    assert(metrics.manager_restarts == 1 && metrics.requests == 4);
    assert(metrics.responses == 2 && metrics.latency_sum_ms == 521);
    Test_Ready = false;
    HostNodeManager_Process();
    assert(HostNodeManager_GetMetrics().ready_losses == 1);
    Test_WriteError = -EIO;
    assert(HostNodeManager_TransmitPending(false) == -EIO);
    assert(HostNodeManager_GetMetrics().requests == 4);
    HostNodeManager_Lifetime.requests = UINT32_MAX;
    HostNodeManager_Count(&HostNodeManager_Lifetime.requests);
    assert(HostNodeManager_GetMetrics().requests == UINT32_MAX);
    puts("PASS: lifetime counters, correlated latency, retries, rejection and saturation");
    return 0;
}
