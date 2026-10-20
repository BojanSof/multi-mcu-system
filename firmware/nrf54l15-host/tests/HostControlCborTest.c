#include <assert.h>
#include <stdio.h>
#define HOST_CONTROL_NATIVE_TEST 1
#define HOST_CONTROL_REAL_CBOR 1
#include "../src/control/HostControl.c"

static HostNodeManager_Status Test_Status;
HostNodeManager_Status HostNodeManager_GetStatus(void) { return Test_Status; }
bool HostNodeManager_IsBusy(void) { return false; }
void HostNodeManager_SetRecordCallback(HostNodeManager_RecordCallback callback) { (void)callback; }
const char *HostNodeManager_StateName(HostLink_State state) {
    (void)state;
    return "online";
}
int HostNodeManager_CallServiceWithOperation(uint16_t service, uint16_t command,
                                             const uint8_t *data, size_t length,
                                             uint32_t operation) {
    (void)service;
    (void)command;
    (void)data;
    (void)length;
    (void)operation;
    return 0;
}

static size_t Test_EncodeCall(uint8_t *buffer, size_t length, bool include_boot, bool duplicate,
                              bool wrong_type, size_t payload_length) {
    uint8_t payload[65] = {0};
    struct zcbor_string bytes = {.value = payload, .len = payload_length};
    ZCBOR_STATE_E(state, 2, buffer, length, 1);
    assert(zcbor_map_start_encode(state, 6));
    assert(zcbor_tstr_put_lit(state, "request_id") && zcbor_uint64_put(state, UINT64_MAX));
    if (include_boot) {
        assert(zcbor_tstr_put_lit(state, "expected_boot_id") && zcbor_uint32_put(state, 42U));
    }
    if (duplicate) {
        assert(zcbor_tstr_put_lit(state, "request_id") && zcbor_uint64_put(state, 7U));
    }
    assert(zcbor_tstr_put_lit(state, "service"));
    assert(wrong_type ? zcbor_tstr_put_lit(state, "invalid") : zcbor_uint32_put(state, 0x1001U));
    assert(zcbor_tstr_put_lit(state, "command") && zcbor_uint32_put(state, 1U));
    assert(zcbor_tstr_put_lit(state, "data") && zcbor_bstr_encode(state, &bytes));
    assert(zcbor_map_end_encode(state, 6));
    return (size_t)(state->payload - buffer);
}

static int Test_Invoke(int (*handler)(struct smp_streamer *), const uint8_t *request, size_t length,
                       size_t *encoded_length) {
    uint8_t output[1016]; /* transport buffer minus eight-byte SMP header */
    ZCBOR_STATE_D(decode, 2, request, length, 1, 0);
    /* Exactly the two nested levels configured for the SMP response writer. */
    ZCBOR_STATE_E(encode, 2, output, sizeof(output), 1);
    struct TestStreamPart reader = {decode}, writer = {encode};
    struct smp_streamer stream = {&reader, &writer};
    assert(zcbor_map_start_encode(encode, 32));
    int result = handler(&stream);
    if (result != 0) {
        return result;
    }
    assert(zcbor_map_end_encode(encode, 32));
    *encoded_length = (size_t)(encode->payload - output);
    assert(*encoded_length < sizeof(output));
    ZCBOR_STATE_D(check, 4, output, *encoded_length, 1, 0);
    assert(zcbor_any_skip(check, NULL));
    assert(check->payload == output + *encoded_length);
    return 0;
}

int main(void) {
    Test_Status.state = HOST_LINK_ONLINE;
    Test_Status.session_generation = 100U;
    Test_Status.service_count = NODE_API_MAX_SERVICES;
    for (size_t index = 0; index < NODE_API_MAX_SERVICES; ++index) {
        Test_Status.services[index] = (NodeApi_ServiceDescriptor){
            .service_id = (uint16_t)(0x1001U + index), .version_major = 1U};
    }
    HostControl_Initialize();
    const uint8_t empty[] = {0xa0};
    size_t encoded;
    assert(Test_Invoke(HostControl_StatusRead, empty, sizeof(empty), &encoded) == 0);
    assert(Test_Invoke(HostControl_ServicesRead, empty, sizeof(empty), &encoded) == 0);
    uint8_t data[NODE_API_SERVICE_DATA_MAX_LENGTH];
    memset(data, 0xff, sizeof(data));
    for (unsigned index = 0; index < HOST_CONTROL_PAGE_SIZE; ++index) {
        HostControl_RecordReceived(HOST_NODE_RECORD_RESPONSE, UINT32_MAX, UINT32_MAX, UINT16_MAX,
                                   UINT16_MAX, UINT32_MAX, data, sizeof(data));
    }
    assert(Test_Invoke(HostControl_EventsRead, empty, sizeof(empty), &encoded) == 0);
    printf("Four maximum-size event records: %zu CBOR bytes\n", encoded);
    uint8_t request[256];
    size_t length = Test_EncodeCall(request, sizeof(request), true, false, false, 64U);
    assert(Test_Invoke(HostControl_CallWrite, request, length, &encoded) == 0);
    assert(HostControl_Queued && HostControl_Mailbox.request == UINT64_MAX &&
           HostControl_Mailbox.length == 64U);
    assert(Test_Invoke(HostControl_CallWrite, request, length, &encoded) == 0);
    assert(HostControl_NextOperation == 1U);
    length = Test_EncodeCall(request, sizeof(request), false, false, false, 0U);
    assert(Test_Invoke(HostControl_CallWrite, request, length, &encoded) == MGMT_ERR_EINVAL);
    length = Test_EncodeCall(request, sizeof(request), true, true, false, 0U);
    assert(Test_Invoke(HostControl_CallWrite, request, length, &encoded) == MGMT_ERR_EINVAL);
    length = Test_EncodeCall(request, sizeof(request), true, false, true, 0U);
    assert(Test_Invoke(HostControl_CallWrite, request, length, &encoded) == MGMT_ERR_EINVAL);
    length = Test_EncodeCall(request, sizeof(request), true, false, false, 65U);
    assert(Test_Invoke(HostControl_CallWrite, request, length, &encoded) == MGMT_ERR_EINVAL);
    length = Test_EncodeCall(request, sizeof(request), true, false, false, 0U);
    assert(Test_Invoke(HostControl_CallWrite, request, length - 1U, &encoded) == MGMT_ERR_EINVAL);
    puts("Real pinned zcbor max-size responses and malformed requests PASS");
    return 0;
}
