#define MULTI_MCU_PHASE12_METRICS 1
#define main HostMetrics_ExistingCborTests
#include "HostControlCborTest.c"
#undef main

static HostNodeManager_Metrics Test_Metrics;
static HostTransport_Stats Test_Transport;
HostNodeManager_Metrics HostNodeManager_GetMetrics(void) { return Test_Metrics; }
HostTransport_Stats HostTransport_GetStats(void) { return Test_Transport; }

int main(void) {
    assert(HostMetrics_ExistingCborTests() == 0);
    memset(&Test_Metrics, 0xff, sizeof(Test_Metrics));
    memset(&Test_Transport, 0xff, sizeof(Test_Transport));
    HostControl_Process();
    const uint8_t empty[] = {0xa0};
    size_t length;
    assert(HostControl_Handlers[7].mh_read == HostControl_MetricsRead);
    assert(HostControl_Handlers[7].mh_write == NULL);
    assert(Test_Invoke(HostControl_MetricsRead, empty, sizeof(empty), &length) == 0);
    assert(length < 1016);
    assert(HostControl_Metrics.latency_sum_ms == UINT64_MAX);
    assert(HostControl_Metrics.requests == UINT32_MAX);
    assert(HostControl_TransportMetrics.uart_errors == UINT32_MAX);
    assert(HostControl_Uptime == 1234);
    assert(!HostControl_StackSupported && HostControl_MainFree == UINT32_MAX);
    /* A handler encodes the main-thread snapshot, not mutable manager state. */
    Test_Metrics.requests = 7;
    assert(Test_Invoke(HostControl_MetricsRead, empty, sizeof(empty), &length) == 0);
    assert(HostControl_Metrics.requests == UINT32_MAX);
    HostControl_Process();
    assert(HostControl_Metrics.requests == 7);
    puts("PASS: metric snapshot, full-width CBOR and bounded SMP response");
    return 0;
}
