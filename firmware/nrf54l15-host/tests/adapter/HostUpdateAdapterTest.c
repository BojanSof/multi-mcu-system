#include <assert.h>
#include <stdio.h>
#define HOST_UPDATE_NATIVE_TEST 1
#define HOST_CONTROL_REAL_CBOR 1
#include "../../src/control/HostUpdateAdapter.c"

int HostTransport_Write(const uint8_t *data, size_t length) {
    (void)data;
    (void)length;
    return 0;
}
size_t HostTransport_Read(uint8_t *data, size_t capacity) {
    (void)data;
    (void)capacity;
    return 0;
}
void HostTransport_Flush(void) {}
static unsigned Test_RuntimeRestartCount;
void HostNodeManager_Initialize(void) { ++Test_RuntimeRestartCount; }
HostNodeManager_Status HostNodeManager_GetStatus(void) {
    return (HostNodeManager_Status){.state = HOST_LINK_ONLINE};
}
void HostStmUpdate_Initialize(HostStmUpdate_Context *core, const HostStmUpdate_Ops *ops) {
    core->ops = *ops;
}
int HostStmUpdate_Begin(HostStmUpdate_Context *core, const uint8_t header[512], uint32_t total) {
    (void)core;
    (void)header;
    (void)total;
    return 0;
}
static unsigned Test_ResetCount;
int HostStmUpdate_Reset(HostStmUpdate_Context *core) {
    Test_ResetCount++;
    core->status.state = HOST_STM_UPDATE_BOOTING;
    core->status.active = true;
    return 0;
}
int HostStmUpdate_Write(HostStmUpdate_Context *core, uint32_t offset, const uint8_t *data,
                        size_t length) {
    (void)core;
    (void)offset;
    (void)data;
    (void)length;
    return 0;
}
int HostStmUpdate_Finish(HostStmUpdate_Context *core) {
    (void)core;
    return 0;
}
int HostStmUpdate_Abort(HostStmUpdate_Context *core) {
    (void)core;
    return 0;
}
void HostStmUpdate_Process(HostStmUpdate_Context *core) { (void)core; }
bool HostStmUpdate_UsesBootTransport(const HostStmUpdate_Context *core) {
    (void)core;
    return false;
}
const char *HostStmUpdate_StateName(HostStmUpdate_State state) {
    (void)state;
    return "querying";
}

static size_t Test_Request(uint8_t *buffer, uint64_t request, uint32_t boot, bool extra) {
    ZCBOR_STATE_E(state, 2, buffer, 1024, 1);
    assert(zcbor_map_start_encode(state, 8));
    assert(zcbor_tstr_put_lit(state, "action") && zcbor_tstr_put_lit(state, "reset"));
    assert(zcbor_tstr_put_lit(state, "request_id") && zcbor_uint64_put(state, request));
    assert(zcbor_tstr_put_lit(state, "expected_boot_id") && zcbor_uint32_put(state, boot));
    if (extra) {
        assert(zcbor_tstr_put_lit(state, "offset") && zcbor_uint32_put(state, 0));
    }
    assert(zcbor_map_end_encode(state, 8));
    return state->payload - buffer;
}
static char Test_Error[32];
static int Test_Invoke(int (*handler)(struct smp_streamer *), const uint8_t *request,
                       size_t length) {
    uint8_t output[1024];
    ZCBOR_STATE_D(decode, 2, request, length, 1, 0);
    ZCBOR_STATE_E(encode, 2, output, sizeof(output), 1);
    struct TestStreamPart reader = {decode}, writer = {encode};
    struct smp_streamer stream = {&reader, &writer};
    assert(zcbor_map_start_encode(encode, 32));
    int result = handler(&stream);
    Test_Error[0] = 0;
    if (result != 0)
        return result;
    assert(zcbor_map_end_encode(encode, 32));
    ZCBOR_STATE_D(check, 2, output, encode->payload - output, 1, 0);
    struct zcbor_string error = {0};
    size_t count;
    struct zcbor_map_decode_key_val keys[] = {
        ZCBOR_MAP_DECODE_KEY_DECODER("error", zcbor_tstr_decode, &error),
    };
    assert(zcbor_map_decode_bulk(check, keys, 1, &count) == 0);
    if (error.len) {
        assert(error.len < sizeof(Test_Error));
        memcpy(Test_Error, error.value, error.len);
        Test_Error[error.len] = 0;
    }
    return 0;
}
int main(void) {
    uint8_t request[1024];
    HostUpdateAdapter_Initialize(42);
    size_t length = Test_Request(request, 1, 42, false);
    assert(Test_Invoke(HostUpdateAdapter_ResetWrite, request, length) == 0);
    assert(strcmp(Test_Error, "unsupported") == 0);
    HostUpdate_Available = true;
    length = Test_Request(request, 1, 41, false);
    assert(Test_Invoke(HostUpdateAdapter_ResetWrite, request, length) == 0);
    assert(strcmp(Test_Error, "host_restarted") == 0 && !HostUpdate_Queued);
    length = Test_Request(request, 1, 42, true);
    assert(Test_Invoke(HostUpdateAdapter_ResetWrite, request, length) == MGMT_ERR_EINVAL);
    length = Test_Request(request, 1, 42, false);
    assert(HostUpdateAdapter_ReserveRuntime());
    assert(Test_Invoke(HostUpdateAdapter_ResetWrite, request, length) == 0);
    assert(strcmp(Test_Error, "busy") == 0);
    HostUpdateAdapter_SetRuntimeBusy(false);
    assert(Test_Invoke(HostUpdateAdapter_ResetWrite, request, length) == 0);
    assert(!Test_Error[0] && HostUpdate_Queued && HostUpdate_Job == 1);
    assert(HostUpdate_Status.active && !HostUpdateAdapter_ReserveRuntime());
    assert(Test_ResetCount == 0);
    HostUpdateAdapter_Process();
    assert(Test_ResetCount == 1);
    assert(Test_Invoke(HostUpdateAdapter_ResetWrite, request, length) == 0);
    HostUpdateAdapter_Process();
    assert(Test_ResetCount == 1);
    puts("Update adapter real CBOR, boot guard, mailbox, exclusion and reset replay PASS");
    return 0;
}
