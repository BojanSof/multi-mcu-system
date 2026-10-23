#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
static int Test_HashStart(void);
static int Test_HashUpdate(const uint8_t *, size_t);
static int Test_HashFinish(uint8_t hash[32]);
static void Test_HashAbort(void);
static int Test_RomMode(bool);
#define MULTI_MCU_PHASE11_ROM 1
#define HOST_ROM_NATIVE_TEST 1
#define main Phase10_TestMain
#include "HostUpdateAdapterTest.c"
#undef main

static int Test_ConnectError;
static int Test_WriteError;
static unsigned Test_Erases;
static unsigned Test_Writes;
static unsigned Test_Restores;
static unsigned Test_Enters;
static int Test_RestoreError;
static uint8_t Test_HashByte;

static int Test_HashStart(void) {
    Test_HashByte = 0;
    return 0;
}
static int Test_HashUpdate(const uint8_t *bytes, size_t length) {
    for (size_t i = 0; i < length; ++i)
        Test_HashByte ^= bytes[i];
    return 0;
}
static int Test_HashFinish(uint8_t hash[32]) {
    memset(hash, 0, 32);
    hash[0] = Test_HashByte;
    return 0;
}
static void Test_HashAbort(void) {}
static int Test_RomMode(bool rom) {
    if (rom)
        ++Test_Enters;
    else
        ++Test_Restores;
    return rom ? 0 : Test_RestoreError;
}
void Stm32RomBoot_Initialize(Stm32RomBoot_Context *context, const Stm32RomBoot_Ops *ops) {
    memset(context, 0, sizeof(*context));
    context->ops = *ops;
}
int Stm32RomBoot_Connect(Stm32RomBoot_Context *context) {
    assert(HostUpdate_Processing);
    context->device_id = 0x489;
    context->version = 0xD0;
    context->connected = Test_ConnectError == 0;
    return Test_ConnectError;
}
int Stm32RomBoot_EraseBootloader(Stm32RomBoot_Context *context) {
    assert(context->connected && HostUpdate_Processing);
    ++Test_Erases;
    return 0;
}
int Stm32RomBoot_WriteVerified(Stm32RomBoot_Context *context, uint32_t offset, const uint8_t *bytes,
                               size_t length) {
    assert(context->connected && HostUpdate_Processing);
    (void)offset;
    (void)bytes;
    (void)length;
    ++Test_Writes;
    return Test_WriteError;
}
int Stm32RomBoot_Read(Stm32RomBoot_Context *context, uint32_t offset, uint8_t *bytes,
                      size_t length) {
    (void)context;
    (void)offset;
    (void)bytes;
    (void)length;
    return 0;
}

static size_t Test_RomRequest(uint8_t *buffer, const char *action, uint64_t id, uint64_t job,
                              uint32_t offset, const uint8_t *data, size_t length,
                              bool prerequisites) {
    ZCBOR_STATE_E(state, 2, buffer, 1024, 1);
    assert(zcbor_map_start_encode(state, 10));
    assert(zcbor_tstr_put_lit(state, "action") && zcbor_tstr_put_term(state, action, 16));
    assert(zcbor_tstr_put_lit(state, "request_id") && zcbor_uint64_put(state, id));
    assert(zcbor_tstr_put_lit(state, "expected_boot_id") && zcbor_uint32_put(state, 42));
    if (strcmp(action, "begin") == 0) {
        struct zcbor_string hash = {data, length};
        assert(zcbor_tstr_put_lit(state, "total") && zcbor_uint32_put(state, offset));
        assert(zcbor_tstr_put_lit(state, "sha256") && zcbor_bstr_encode(state, &hash));
        assert(zcbor_tstr_put_lit(state, "prerequisites") && zcbor_bool_put(state, prerequisites));
    } else {
        assert(zcbor_tstr_put_lit(state, "job") && zcbor_uint64_put(state, job));
        if (strcmp(action, "chunk") == 0) {
            struct zcbor_string value = {data, length};
            assert(zcbor_tstr_put_lit(state, "offset") && zcbor_uint32_put(state, offset));
            assert(zcbor_tstr_put_lit(state, "data") && zcbor_bstr_encode(state, &value));
        }
    }
    assert(zcbor_map_end_encode(state, 10));
    return state->payload - buffer;
}
static void Test_RomBegin(uint64_t id, uint32_t total, uint8_t hash_byte) {
    uint8_t buffer[1024], hash[32] = {0};
    hash[0] = hash_byte;
    size_t size = Test_RomRequest(buffer, "begin", id, 0, total, hash, 32, true);
    assert(Test_Invoke(HostUpdateAdapter_RomWrite, buffer, size) == 0);
    assert(!Test_Error[0] && HostRom_Status.active && HostRom_Job == id);
    assert(HostRom_Status.total == total);
    assert(!HostUpdateAdapter_ReserveRuntime());
    HostUpdateAdapter_Process();
}

int main(void) {
    (void)Phase10_TestMain();
    HostUpdate_Status.active = false;
    HostUpdate_RuntimeBusy = false;
    uint8_t buffer[1024], hash[32] = {0};
    size_t size = Test_RomRequest(buffer, "begin", 10, 0, 8, hash, 32, false);
    assert(Test_Invoke(HostUpdateAdapter_RomWrite, buffer, size) == MGMT_ERR_EINVAL);
    assert(Test_Enters == 0);
    HostUpdate_RuntimeBusy = true;
    size = Test_RomRequest(buffer, "begin", 10, 0, 8, hash, 32, true);
    assert(Test_Invoke(HostUpdateAdapter_RomWrite, buffer, size) == 0);
    assert(strcmp(Test_Error, "busy") == 0 && Test_Enters == 0);
    HostUpdate_RuntimeBusy = false;
    Test_RomBegin(10, 9, 1);
    assert(HostRom_Status.state == HOST_STM_UPDATE_READY && Test_Erases == 1);
    assert(HostRom_DeviceId == 0x489 && HostRom_Version == 0xD0);
    size = Test_Request(buffer, 99, 42, false);
    assert(Test_Invoke(HostUpdateAdapter_ResetWrite, buffer, size) == 0);
    assert(strcmp(Test_Error, "busy") == 0);
    uint8_t data[9] = {1};
    size = Test_RomRequest(buffer, "chunk", 11, 10, 0, data, 9, true);
    assert(Test_Invoke(HostUpdateAdapter_RomWrite, buffer, size) == 0 && !Test_Error[0]);
    assert(HostRom_Status.offset == 0); /* admission isn't readback */
    HostUpdateAdapter_Process();
    assert(HostRom_Status.offset == 9 && Test_Writes == 1);
    assert(Test_Invoke(HostUpdateAdapter_RomWrite, buffer, size) == 0 && !Test_Error[0]);
    HostUpdateAdapter_Process();
    assert(Test_Writes == 1); /* retry doesn't program twice */
    size = Test_RomRequest(buffer, "finish", 12, 10, 0, NULL, 0, true);
    assert(Test_Invoke(HostUpdateAdapter_RomWrite, buffer, size) == 0 && !Test_Error[0]);
    HostUpdateAdapter_Process();
    assert(HostRom_Status.state == HOST_STM_UPDATE_COMPLETE && !HostRom_Status.active);
    assert(Test_Restores == 1 && !HostUpdateAdapter_UsesBootTransport());
    Test_ConnectError = -ENODEV;
    Test_RomBegin(20, 8, 0);
    assert(HostRom_Status.state == HOST_STM_UPDATE_FAILED && Test_Erases == 1);
    assert(Test_Restores == 2);
    Test_ConnectError = 0;
    Test_RomBegin(30, 8, 0);
    Test_WriteError = -EBADMSG;
    size = Test_RomRequest(buffer, "chunk", 31, 30, 0, data, 8, true);
    assert(Test_Invoke(HostUpdateAdapter_RomWrite, buffer, size) == 0 && !Test_Error[0]);
    HostUpdateAdapter_Process();
    assert(HostRom_Status.state == HOST_STM_UPDATE_FAILED && HostRom_Status.offset == 0);
    assert(Test_Restores == 3);
    Test_WriteError = 0;
    Test_RomBegin(40, 8, 0);
    size = Test_RomRequest(buffer, "abort", 41, 40, 0, NULL, 0, true);
    assert(Test_Invoke(HostUpdateAdapter_RomWrite, buffer, size) == 0 && !Test_Error[0]);
    HostUpdateAdapter_Process();
    assert(HostRom_Status.state == HOST_STM_UPDATE_ABORTED && Test_Restores == 4);
    Test_RomBegin(50, 8, 0);
    size = Test_RomRequest(buffer, "chunk", 51, 50, 0, data, 8, true);
    assert(Test_Invoke(HostUpdateAdapter_RomWrite, buffer, size) == 0 && !Test_Error[0]);
    HostUpdateAdapter_Process();
    size = Test_RomRequest(buffer, "finish", 52, 50, 0, NULL, 0, true);
    assert(Test_Invoke(HostUpdateAdapter_RomWrite, buffer, size) == 0 && !Test_Error[0]);
    HostUpdateAdapter_Process();
    assert(HostRom_Status.state == HOST_STM_UPDATE_FAILED && HostRom_Status.error == -EBADMSG);
    assert(Test_Restores == 5);
    Test_RomBegin(60, 8, 0);
    HostRom_Activity -= 30001;
    HostUpdateAdapter_Process();
    assert(HostRom_Status.state == HOST_STM_UPDATE_ABORTED && Test_Restores == 6);
    Test_RestoreError = -EIO;
    Test_ConnectError = -ENODEV;
    const unsigned restarts = Test_RuntimeRestartCount;
    Test_RomBegin(70, 8, 0);
    assert(HostRom_Status.state == HOST_STM_UPDATE_FAILED && HostRom_Status.active);
    assert(HostRom_Status.error == -EIO && Test_Restores == 7);
    assert(HostUpdateAdapter_IsBusy() && HostUpdateAdapter_UsesBootTransport());
    assert(!HostUpdateAdapter_ReserveRuntime() && Test_RuntimeRestartCount == restarts);
    HostRom_Activity -= 30001;
    for (unsigned i = 0; i < 10; ++i)
        HostUpdateAdapter_Process();
    assert(Test_Restores == 7 && Test_RuntimeRestartCount == restarts);
    size = Test_Request(buffer, 79, 42, false);
    assert(Test_Invoke(HostUpdateAdapter_ResetWrite, buffer, size) == 0);
    assert(strcmp(Test_Error, "busy") == 0);
    Test_RestoreError = 0;
    size = Test_RomRequest(buffer, "abort", 71, 70, 0, NULL, 0, true);
    assert(Test_Invoke(HostUpdateAdapter_RomWrite, buffer, size) == 0 && !Test_Error[0]);
    HostUpdateAdapter_Process();
    assert(HostRom_Status.state == HOST_STM_UPDATE_ABORTED && !HostRom_Status.active);
    assert(!HostUpdateAdapter_UsesBootTransport() && Test_RuntimeRestartCount == restarts + 1);
    puts("ROM adapter admission, replay, readback offsets, exclusion and restoration PASS");
    return 0;
}
