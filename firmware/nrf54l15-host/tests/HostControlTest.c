#include <assert.h>
#include <stdio.h>
#define HOST_CONTROL_NATIVE_TEST 1
#include "../src/control/HostControl.c"

static HostNodeManager_Status Test_Status;
static bool Test_Busy;
static unsigned Test_Executions;
static int Test_AdmissionResult;
static HostNodeManager_RecordCallback Test_Callback;

HostNodeManager_Status HostNodeManager_GetStatus(void) { return Test_Status; }
bool HostNodeManager_IsBusy(void) { return Test_Busy; }
void HostNodeManager_SetRecordCallback(HostNodeManager_RecordCallback callback) {
    Test_Callback = callback;
}
const char *HostNodeManager_StateName(HostLink_State state) {
    return state == HOST_LINK_ONLINE ? "online" : "discovering";
}
int HostNodeManager_CallServiceWithOperation(uint16_t service, uint16_t command,
                                             const uint8_t *data, size_t length,
                                             uint32_t operation) {
    (void)service;
    (void)command;
    (void)data;
    (void)length;
    assert(operation == Test_Operation);
    Test_Executions++;
    Test_Busy = Test_AdmissionResult == 0;
    return Test_AdmissionResult;
}

static void Test_Reset(void) {
    memset(HostControl_Cache, 0, sizeof(HostControl_Cache));
    memset(HostControl_History, 0, sizeof(HostControl_History));
    memset(&HostControl_AsyncActuator, 0, sizeof(HostControl_AsyncActuator));
    HostControl_CacheNext = HostControl_Count = HostControl_HistoryHead = 0U;
    HostControl_Latest = HostControl_Dropped = HostControl_NextOperation = 0U;
    HostControl_Queued = HostControl_Busy = Test_Busy = false;
    Test_Status =
        (HostNodeManager_Status){.state = HOST_LINK_ONLINE,
                                 .session_generation = 100U,
                                 .service_count = 2U,
                                 .services = {{.service_id = 0x1001U}, {.service_id = 0x1002U}}};
    Test_Request = 1U;
    Test_Boot = 42U;
    Test_Service = 0x1001U;
    Test_Command = 1U;
    Test_Length = Test_Executions = Test_EventMaps = 0U;
    Test_AdmissionResult = 0;
    Test_Malformed = false;
    Test_Error[0] = Test_LastKey[0] = 0;
    HostControl_Initialize();
}

int main(void) {
    zcbor_state_t state = {0};
    struct TestStreamPart part = {&state};
    struct smp_streamer stream = {&part, &part};
    Test_Reset();
    assert(HostControl_CallWrite(&stream) == 0 && HostControl_Queued && Test_Operation == 1U);
    assert(HostControl_CallWrite(&stream) == 0 && Test_Operation == 1U);
    HostControl_Process();
    assert(Test_Executions == 1U);
    assert(HostControl_CallWrite(&stream) == 0 && Test_Executions == 1U);
    Test_Length = 1U;
    assert(HostControl_CallWrite(&stream) == 0 && strcmp(Test_Error, "request_conflict") == 0);
    Test_Request = 2U;
    Test_Length = 0U;
    assert(HostControl_CallWrite(&stream) == 0 && strcmp(Test_Error, "busy") == 0);
    assert(HostControl_CacheNext == 1U);
    Test_Callback(HOST_NODE_RECORD_RESPONSE, 100U, 1U, 0x1001U, 1U, 0U, NULL, 0U);
    Test_Busy = false;
    HostControl_Process();
    assert(HostControl_CallWrite(&stream) == 0 && Test_Operation == 2U);
    HostControl_Process();
    assert(Test_Executions == 2U);

    Test_Reset();
    Test_Boot = 7U;
    assert(HostControl_CallWrite(&stream) == 0 && strcmp(Test_Error, "host_restarted") == 0);
    assert(!HostControl_Queued && Test_Executions == 0U);
    Test_Boot = 42U;
    Test_Length = 65U;
    assert(HostControl_CallWrite(&stream) == MGMT_ERR_EINVAL);
    Test_Length = 0U;
    Test_Malformed = true;
    assert(HostControl_CallWrite(&stream) == MGMT_ERR_EINVAL);
    Test_Malformed = false;
    Test_Service = 0x7777U;
    assert(HostControl_CallWrite(&stream) == 0 && strcmp(Test_Error, "unsupported") == 0);
    Test_Status.state = HOST_LINK_DISCOVERING;
    HostControl_Process();
    assert(HostControl_CallWrite(&stream) == 0 && strcmp(Test_Error, "offline") == 0);

    Test_Reset();
    assert(HostControl_CallWrite(&stream) == 0);
    Test_Status.session_generation = 101U;
    HostControl_Process();
    assert(Test_Executions == 0U && HostControl_Count >= 1U);
    assert(HostControl_History[0].kind == HOST_NODE_RECORD_ERROR);

    Test_Reset();
    Test_Service = VIRTUAL_IO_ACTUATOR_SERVICE_ID;
    assert(HostControl_CallWrite(&stream) == 0);
    HostControl_Process();
    Test_Callback(HOST_NODE_RECORD_RESPONSE, 100U, 1U, Test_Service, Test_Command, 0U, NULL, 0U);
    assert(HostControl_Cache[0].pending);
    Test_Status.session_generation = 0U;
    HostControl_Process();
    assert(!HostControl_Cache[0].pending && HostControl_History[1].status == ECANCELED);

    Test_Reset();
    for (unsigned i = 0U; i < 20U; ++i) {
        Test_Callback(HOST_NODE_RECORD_EVENT, 100U, i, 1U, 1U, 0U, NULL, 0U);
    }
    assert(HostControl_Count == 16U && HostControl_Dropped == 4U && HostControl_Latest == 20U);
    Test_After = 0U;
    assert(HostControl_EventsRead(&stream) == 0 && Test_EventMaps == 4U);
    Test_EventMaps = 0U;
    Test_After = 18U;
    assert(HostControl_EventsRead(&stream) == 0 && Test_EventMaps == 2U);
    Test_Reset();
    HostControl_Latest = UINT32_MAX - 1U;
    Test_Callback(HOST_NODE_RECORD_EVENT, 100U, 0U, 1U, 1U, 0U, NULL, 0U);
    Test_Callback(HOST_NODE_RECORD_EVENT, 100U, 0U, 1U, 1U, 0U, NULL, 0U);
    assert(HostControl_Latest == 1U && HostControl_Count == 2U);
    Test_After = UINT32_MAX;
    assert(HostControl_EventsRead(&stream) == 0 && Test_EventMaps == 1U);
    Test_Reset();
    for (unsigned index = 0U; index < 9U; ++index) {
        Test_Request = index + 1U;
        assert(HostControl_CallWrite(&stream) == 0);
        HostControl_Process();
        Test_Callback(HOST_NODE_RECORD_RESPONSE, 100U, Test_Operation, Test_Service, Test_Command,
                      0U, NULL, 0U);
        Test_Busy = false;
        HostControl_Process();
    }
    assert(Test_Executions == 9U && HostControl_CacheNext == 1U);
    Test_Request = 9U;
    assert(HostControl_CallWrite(&stream) == 0 && Test_Operation == 9U);
    assert(!HostControl_Queued && Test_Executions == 9U);
    assert(HostControl_Unsupported(&stream) == 0 && strcmp(Test_Error, "unsupported") == 0);
    puts("Host control admission, replay, session cancellation, history and validation PASS");
    return 0;
}
