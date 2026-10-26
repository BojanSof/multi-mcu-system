#define MULTI_MCU_RELIABILITY 1
#define MULTI_MCU_PHASE10_UPDATE 1
#define main HostRecovery_UnusedLegacyTests
#include "HostControlCborTest.c"
#undef main
static bool Test_UpdateBusy;
static bool Test_RuntimeBusy;
static unsigned Test_Clears;
static unsigned Test_Reserves;
void HostUpdateAdapter_Initialize(uint32_t boot_id) { (void)boot_id; }
void HostUpdateAdapter_SetRuntimeBusy(bool busy) { Test_RuntimeBusy = busy; }
bool HostUpdateAdapter_IsBusy(void) { return Test_UpdateBusy; }
bool HostUpdateAdapter_IsAvailable(void) { return true; }
bool HostUpdateAdapter_ReserveRuntime(void) {
    ++Test_Reserves;
    return !Test_UpdateBusy;
}
int HostUpdateAdapter_StatusRead(struct smp_streamer *stream) {
    (void)stream;
    return 0;
}
int HostUpdateAdapter_ResetWrite(struct smp_streamer *stream) {
    (void)stream;
    return 0;
}
int HostUpdateAdapter_UpdateWrite(struct smp_streamer *stream) {
    (void)stream;
    return 0;
}
HostReliability_Status HostReliability_GetStatus(void) {
    return (HostReliability_Status){.health = "failed",
                                    .reason = "reset_limit",
                                    .recovery_attempts = 3U,
                                    .watchdog = true,
                                    .reset_cause = 2U,
                                    .failed = true};
}
void HostReliability_Clear(void) { ++Test_Clears; }
static size_t Test_ClearBody(uint8_t *buffer, uint32_t boot, uint64_t request, const char *action) {
    ZCBOR_STATE_E(state, 2, buffer, 128, 1);
    assert(zcbor_map_start_encode(state, 3));
    assert(zcbor_tstr_put_lit(state, "expected_boot_id") && zcbor_uint32_put(state, boot));
    assert(zcbor_tstr_put_lit(state, "request_id") && zcbor_uint64_put(state, request));
    assert(zcbor_tstr_put_lit(state, "action") && zcbor_tstr_put_term(state, action, 32));
    assert(zcbor_map_end_encode(state, 3));
    return (size_t)(state->payload - buffer);
}
int main(void) {
    HostControl_Initialize();
    size_t result_length;
    const uint8_t empty[] = {0xa0};
    assert(Test_Invoke(HostControl_RecoveryRead, empty, sizeof(empty), &result_length) == 0);
    assert(Test_Invoke(HostControl_StatusRead, empty, sizeof(empty), &result_length) == 0);
    uint8_t body[128];
    size_t length = Test_ClearBody(body, 99, 1, "clear");
    assert(Test_Invoke(HostControl_RecoveryWrite, body, length, &result_length) == 0);
    assert(!HostControl_ClearQueued && Test_Reserves == 0);
    length = Test_ClearBody(body, 42, 0, "clear");
    assert(Test_Invoke(HostControl_RecoveryWrite, body, length, &result_length) == MGMT_ERR_EINVAL);
    length = Test_ClearBody(body, 42, 1, "bad");
    assert(Test_Invoke(HostControl_RecoveryWrite, body, length, &result_length) == MGMT_ERR_EINVAL);
    Test_UpdateBusy = true;
    length = Test_ClearBody(body, 42, 1, "clear");
    assert(Test_Invoke(HostControl_RecoveryWrite, body, length, &result_length) == 0);
    assert(!HostControl_ClearQueued);
    Test_UpdateBusy = false;
    assert(Test_Invoke(HostControl_RecoveryWrite, body, length, &result_length) == 0);
    assert(HostControl_ClearQueued);
    HostControl_Process();
    assert(Test_Clears == 1);
    assert(Test_Invoke(HostControl_RecoveryWrite, body, length, &result_length) == 0);
    HostControl_Process();
    assert(Test_Clears == 1);
    return 0;
}
