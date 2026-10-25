#include "HostUpdateAdapter.h"
#include "HostNodeManager.h"
#include "HostStmUpdate.h"
#include "HostTransport.h"
#include "firmware_image/FirmwareImage.h"
#include "node_api/NodeCommands.h"
#include "protocol/ProtocolTypes.h"
#include <errno.h>
#if defined(MULTI_MCU_PHASE11_ROM)
#include "stm32_rom_boot/Stm32RomBoot.h"
#if !defined(HOST_ROM_NATIVE_TEST)
#include <psa/crypto.h>
#endif
#endif
#if defined(HOST_UPDATE_NATIVE_TEST)
#include "HostControlTestSdk.h"
#define HOST_UPDATE_HAS_GPIO 0
#else
#include <string.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>

#include <mgmt/mcumgr/util/zcbor_bulk.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/mgmt/mcumgr/smp/smp.h>
#endif

#define HOST_UPDATE_CACHE_SIZE 8U
#if !defined(HOST_UPDATE_NATIVE_TEST)
#define HOST_UPDATE_USER DT_PATH(zephyr_user)
#if DT_NODE_HAS_PROP(HOST_UPDATE_USER, node_reset_gpios) &&                                        \
    DT_NODE_HAS_PROP(HOST_UPDATE_USER, node_boot_gpios)
#define HOST_UPDATE_HAS_GPIO 1
static const struct gpio_dt_spec HostUpdate_Reset =
    GPIO_DT_SPEC_GET(HOST_UPDATE_USER, node_reset_gpios);
static const struct gpio_dt_spec HostUpdate_Boot =
    GPIO_DT_SPEC_GET(HOST_UPDATE_USER, node_boot_gpios);
#endif
#endif

typedef enum {
    HOST_UPDATE_RESET,
    HOST_UPDATE_BEGIN,
    HOST_UPDATE_CHUNK,
    HOST_UPDATE_FINISH,
    HOST_UPDATE_ABORT,
    HOST_UPDATE_ROM_BEGIN,
    HOST_UPDATE_ROM_CHUNK,
    HOST_UPDATE_ROM_FINISH,
    HOST_UPDATE_ROM_ABORT
} HostUpdate_Action;
typedef struct {
    uint64_t request;
    uint64_t job;
    uint32_t offset;
    uint32_t total;
    HostUpdate_Action action;
    size_t length;
    uint8_t data[512];
} HostUpdate_Request;

static K_MUTEX_DEFINE(HostUpdate_Lock);
static HostStmUpdate_Context HostUpdate_Core;
static HostStmUpdate_Status HostUpdate_Status;
static HostUpdate_Request HostUpdate_Cache[HOST_UPDATE_CACHE_SIZE];
static HostUpdate_Request HostUpdate_Mailbox;
static HostUpdate_Request HostUpdate_StartRequest;
static size_t HostUpdate_CacheNext;
static uint32_t HostUpdate_BootId;
static uint64_t HostUpdate_Job;
static bool HostUpdate_Queued;
static bool HostUpdate_Available;
static bool HostUpdate_BootOwned;
static bool HostUpdate_ResetJob;
static bool HostUpdate_RuntimeBusy;
static bool HostUpdate_Processing;
#if defined(MULTI_MCU_PHASE12_METRICS)
typedef struct {
    int64_t start;
    uint64_t elapsed;
    bool live;
} HostUpdate_Timing;
static HostUpdate_Timing HostUpdate_JobTiming;
static HostUpdate_Timing HostRom_JobTiming;

static void HostUpdate_StartTiming(HostUpdate_Timing *timing) {
    timing->start = k_uptime_get();
    timing->elapsed = 0;
    timing->live = true;
}
static uint64_t HostUpdate_Elapsed(const HostUpdate_Timing *timing) {
    int64_t elapsed = k_uptime_get() - timing->start;
    return timing->live ? (elapsed > 0 ? (uint64_t)elapsed : 0U) : timing->elapsed;
}
static void HostUpdate_StopTiming(HostUpdate_Timing *timing, bool active) {
    if (timing->live && !active) {
        timing->elapsed = HostUpdate_Elapsed(timing);
        timing->live = false;
    }
}
static bool HostUpdate_EncodeTiming(zcbor_state_t *state, const HostUpdate_Timing *timing,
                                    uint32_t offset) {
    uint64_t elapsed = HostUpdate_Elapsed(timing);
    uint64_t rate = elapsed == 0 ? 0U : (uint64_t)offset * 1000U / elapsed;
    return zcbor_tstr_put_lit(state, "elapsed_ms") && zcbor_uint64_put(state, elapsed) &&
           zcbor_tstr_put_lit(state, "bytes_per_second") && zcbor_uint64_put(state, rate);
}
#endif

#if defined(MULTI_MCU_PHASE11_ROM)
static Stm32RomBoot_Context HostRom_Core;
static HostStmUpdate_Status HostRom_Status;
static uint64_t HostRom_Job;
static uint8_t HostRom_ExpectedHash[32];
static int64_t HostRom_Activity;
static uint16_t HostRom_DeviceId;
static uint8_t HostRom_Version;
#if !defined(HOST_ROM_NATIVE_TEST)
static psa_hash_operation_t HostRom_Hash = PSA_HASH_OPERATION_INIT;
#endif

static int HostRom_HashStart(void) {
#if defined(HOST_ROM_NATIVE_TEST)
    return Test_HashStart();
#else
    (void)psa_hash_abort(&HostRom_Hash);
    if (psa_crypto_init() != PSA_SUCCESS) {
        return -EIO;
    }
    return psa_hash_setup(&HostRom_Hash, PSA_ALG_SHA_256) == PSA_SUCCESS ? 0 : -EIO;
#endif
}
static int HostRom_HashUpdate(const uint8_t *data, size_t length) {
#if defined(HOST_ROM_NATIVE_TEST)
    return Test_HashUpdate(data, length);
#else
    return psa_hash_update(&HostRom_Hash, data, length) == PSA_SUCCESS ? 0 : -EIO;
#endif
}
static int HostRom_HashFinish(uint8_t hash[32]) {
#if defined(HOST_ROM_NATIVE_TEST)
    return Test_HashFinish(hash);
#else
    size_t length = 0;
    return psa_hash_finish(&HostRom_Hash, hash, 32, &length) == PSA_SUCCESS && length == 32 ? 0
                                                                                            : -EIO;
#endif
}
static void HostRom_HashAbort(void) {
#if defined(HOST_ROM_NATIVE_TEST)
    Test_HashAbort();
#else
    (void)psa_hash_abort(&HostRom_Hash);
#endif
}
static bool HostRom_IsBusy(void) { return HostRom_Status.active; }
#else
static bool HostRom_IsBusy(void) { return false; }
#endif

static int HostUpdate_Send(void *context, const uint8_t *data, size_t length) {
    (void)context;
    return HostTransport_Write(data, length);
}
static size_t HostUpdate_Read(void *context, uint8_t *data, size_t capacity) {
    (void)context;
    return HostTransport_Read(data, capacity);
}
static int64_t HostUpdate_Now(void *context) {
    (void)context;
    return k_uptime_get();
}
static int HostUpdate_Pulse(void *context) {
    (void)context;
#if HOST_UPDATE_HAS_GPIO
    if (!HostUpdate_Available) {
        return -ENODEV;
    }
    /* Phase 10 enters the custom loader only. BOOT0 is never asserted. */
    int result = gpio_pin_set_dt(&HostUpdate_Boot, 0);
    if (result == 0) {
        result = gpio_pin_set_dt(&HostUpdate_Reset, 1);
    }
    if (result != 0) {
        (void)gpio_pin_set_dt(&HostUpdate_Reset, 0);
        return result;
    }
    HostTransport_Flush();
    k_msleep(20);
    return gpio_pin_set_dt(&HostUpdate_Reset, 0);
#else
    return -ENODEV;
#endif
}
static void HostUpdate_RuntimeRestart(void *context) {
    (void)context;
    HostTransport_Flush();
    HostNodeManager_Initialize();
}
static bool HostUpdate_RuntimeOnline(void *context) {
    (void)context;
    return HostNodeManager_GetStatus().state == HOST_LINK_ONLINE;
}

#if defined(MULTI_MCU_PHASE11_ROM)
static int HostRom_ReadExact(void *context, uint8_t *data, size_t length, uint32_t timeout) {
    (void)context;
    size_t offset = 0;
    const int64_t deadline = k_uptime_get() + timeout;
    while (offset < length) {
        offset += HostTransport_Read(data + offset, length - offset);
        if (offset == length) {
            return 0;
        }
        if (k_uptime_get() >= deadline) {
            return -ETIMEDOUT;
        }
#if !defined(HOST_ROM_NATIVE_TEST)
        k_msleep(1);
#endif
    }
    return 0;
}
static int HostRom_Mode(bool rom) {
#if defined(HOST_ROM_NATIVE_TEST)
    return Test_RomMode(rom);
#elif HOST_UPDATE_HAS_GPIO
    int result = HostTransport_ConfigureRom(rom);
    if (result == 0) {
        result = gpio_pin_set_dt(&HostUpdate_Boot, rom ? 1 : 0);
    }
    if (result == 0) {
        result = gpio_pin_set_dt(&HostUpdate_Reset, 1);
    }
    if (result == 0) {
        HostTransport_Flush();
        k_msleep(20);
    }
    int released = gpio_pin_set_dt(&HostUpdate_Reset, 0);
    if (result == 0) {
        result = released;
    }
    k_msleep(20);
    return result;
#else
    (void)rom;
    return -ENODEV;
#endif
}
static void HostRom_Exit(HostStmUpdate_Status *status, HostStmUpdate_State outcome, int error) {
    HostRom_HashAbort();
    /* Every path attempts all restoration steps, even if parity setup or BOOT0 failed. */
#if !defined(HOST_ROM_NATIVE_TEST) && HOST_UPDATE_HAS_GPIO
    int boot = gpio_pin_set_dt(&HostUpdate_Boot, 0);
    int parity = HostTransport_ConfigureRom(false);
    int reset = HostUpdate_Pulse(NULL);
    int restore = boot != 0 ? boot : parity != 0 ? parity : reset;
#else
    int restore = HostRom_Mode(false);
#endif
    HostRom_Core.connected = false;
    if (restore != 0) {
        /* Unknown BOOT0/parity/reset state must never release UART ownership.
         * Latch the failure; only explicit abort retries restoration. */
        status->state = HOST_STM_UPDATE_FAILED;
        status->error = restore;
        status->active = true;
        return;
    }
    HostUpdate_RuntimeRestart(NULL);
    status->state = error != 0 ? HOST_STM_UPDATE_FAILED : outcome;
    status->error = error;
    status->active = false;
}
static void HostRom_ProcessRequest(const HostUpdate_Request *request, bool queued) {
    /* Only main accesses the ROM context/hash. SMP reads the locked snapshot. */
    k_mutex_lock(&HostUpdate_Lock, K_FOREVER);
    HostStmUpdate_Status status = HostRom_Status;
    k_mutex_unlock(&HostUpdate_Lock);
    int result = 0;
    if (queued) {
        switch (request->action) {
        case HOST_UPDATE_ROM_BEGIN: {
            const Stm32RomBoot_Ops ops = {.send = HostUpdate_Send, .read = HostRom_ReadExact};
            Stm32RomBoot_Initialize(&HostRom_Core, &ops);
            memcpy(HostRom_ExpectedHash, request->data, 32);
            result = HostRom_HashStart();
            if (result == 0) {
                result = HostRom_Mode(true);
            }
            if (result == 0) {
                result = Stm32RomBoot_Connect(&HostRom_Core);
            }
            if (result == 0) {
                status.state = HOST_STM_UPDATE_ERASING;
                k_mutex_lock(&HostUpdate_Lock, K_FOREVER);
                HostRom_Status = status;
                k_mutex_unlock(&HostUpdate_Lock);
                result = Stm32RomBoot_EraseBootloader(&HostRom_Core);
            }
            if (result == 0) {
                status.state = HOST_STM_UPDATE_READY;
            }
            break;
        }
        case HOST_UPDATE_ROM_CHUNK:
            result = Stm32RomBoot_WriteVerified(&HostRom_Core, request->offset, request->data,
                                                request->length);
            if (result == 0) {
                result = HostRom_HashUpdate(request->data, request->length);
            }
            if (result == 0) {
                status.offset += (uint32_t)request->length;
                status.state = HOST_STM_UPDATE_READY;
            }
            break;
        case HOST_UPDATE_ROM_FINISH: {
            uint8_t actual[32];
            result = HostRom_HashFinish(actual);
            if (result == 0 && memcmp(actual, HostRom_ExpectedHash, 32) != 0) {
                result = -EBADMSG;
            }
            if (result == 0) {
                HostRom_Exit(&status, HOST_STM_UPDATE_COMPLETE, 0);
            }
            break;
        }
        case HOST_UPDATE_ROM_ABORT:
            HostRom_Exit(&status, HOST_STM_UPDATE_ABORTED, 0);
            break;
        default:
            break;
        }
        HostRom_Activity = k_uptime_get();
    }
    if (result != 0) {
        HostRom_Exit(&status, HOST_STM_UPDATE_FAILED, result);
    } else if (status.active && status.state != HOST_STM_UPDATE_FAILED &&
               k_uptime_get() - HostRom_Activity >= 30000) {
        HostRom_Exit(&status, HOST_STM_UPDATE_ABORTED, 0);
    }
    k_mutex_lock(&HostUpdate_Lock, K_FOREVER);
    HostRom_Status = status;
#if defined(MULTI_MCU_PHASE12_METRICS)
    HostUpdate_StopTiming(&HostRom_JobTiming, status.active);
#endif
    HostRom_DeviceId = HostRom_Core.device_id;
    HostRom_Version = HostRom_Core.version;
    HostUpdate_BootOwned = status.active;
    k_mutex_unlock(&HostUpdate_Lock);
}
#endif

void HostUpdateAdapter_Initialize(uint32_t boot_id) {
    HostUpdate_BootId = boot_id;
#if HOST_UPDATE_HAS_GPIO
    if (gpio_is_ready_dt(&HostUpdate_Reset) && gpio_is_ready_dt(&HostUpdate_Boot) &&
        gpio_pin_configure_dt(&HostUpdate_Boot, GPIO_OUTPUT_INACTIVE) == 0 &&
        gpio_pin_configure_dt(&HostUpdate_Reset, GPIO_OUTPUT_INACTIVE | GPIO_OPEN_DRAIN) == 0) {
        HostUpdate_Available = HostTransport_IsAvailable();
    }
#endif
    const HostStmUpdate_Ops ops = {.send = HostUpdate_Send,
                                   .read = HostUpdate_Read,
                                   .now = HostUpdate_Now,
                                   .reset = HostUpdate_Pulse,
                                   .runtime_restart = HostUpdate_RuntimeRestart,
                                   .runtime_online = HostUpdate_RuntimeOnline};
    HostStmUpdate_Initialize(&HostUpdate_Core, &ops);
    HostUpdate_Status = HostUpdate_Core.status;
#if defined(MULTI_MCU_PHASE11_ROM)
    HostRom_Status.slot = 255;
    HostRom_Status.confirmed_slot = 255;
#endif
}

bool HostUpdateAdapter_IsAvailable(void) { return HostUpdate_Available; }
bool HostUpdateAdapter_ReserveRuntime(void) {
    k_mutex_lock(&HostUpdate_Lock, K_FOREVER);
    bool admitted = !HostUpdate_Queued && !HostUpdate_Processing && !HostUpdate_Status.active &&
                    !HostRom_IsBusy();
    if (admitted) {
        HostUpdate_RuntimeBusy = true;
    }
    k_mutex_unlock(&HostUpdate_Lock);
    return admitted;
}
void HostUpdateAdapter_SetRuntimeBusy(bool busy) {
    k_mutex_lock(&HostUpdate_Lock, K_FOREVER);
    HostUpdate_RuntimeBusy = busy;
    k_mutex_unlock(&HostUpdate_Lock);
}
bool HostUpdateAdapter_IsBusy(void) {
    k_mutex_lock(&HostUpdate_Lock, K_FOREVER);
    bool busy =
        HostUpdate_Queued || HostUpdate_Processing || HostUpdate_Status.active || HostRom_IsBusy();
    k_mutex_unlock(&HostUpdate_Lock);
    return busy;
}
bool HostUpdateAdapter_UsesBootTransport(void) { return HostUpdate_BootOwned; }

void HostUpdateAdapter_Process(void) {
    HostUpdate_Request request;
    k_mutex_lock(&HostUpdate_Lock, K_FOREVER);
    bool queued = HostUpdate_Queued;
    if (queued) {
        request = HostUpdate_Mailbox;
        /* Keep queued asserted until processing completes to exclude SMP admission. */
    }
    HostUpdate_Processing = true;
    k_mutex_unlock(&HostUpdate_Lock);
#if defined(MULTI_MCU_PHASE11_ROM)
    if ((queued && request.action >= HOST_UPDATE_ROM_BEGIN) || HostRom_IsBusy()) {
        HostRom_ProcessRequest(&request, queued);
        k_mutex_lock(&HostUpdate_Lock, K_FOREVER);
        HostUpdate_Processing = false;
        HostUpdate_Queued = false;
        k_mutex_unlock(&HostUpdate_Lock);
        return;
    }
#endif
    int result = 0;
    if (queued) {
        switch (request.action) {
        case HOST_UPDATE_RESET:
            result = HostStmUpdate_Reset(&HostUpdate_Core);
            break;
        case HOST_UPDATE_BEGIN:
            result = HostStmUpdate_Begin(&HostUpdate_Core, request.data, request.total);
            break;
        case HOST_UPDATE_CHUNK:
            result =
                HostStmUpdate_Write(&HostUpdate_Core, request.offset, request.data, request.length);
            break;
        case HOST_UPDATE_FINISH:
            result = HostStmUpdate_Finish(&HostUpdate_Core);
            break;
        case HOST_UPDATE_ABORT:
            result = HostStmUpdate_Abort(&HostUpdate_Core);
            break;
        default:
            result = -EINVAL;
            break;
        }
    }
    HostStmUpdate_Process(&HostUpdate_Core);
    k_mutex_lock(&HostUpdate_Lock, K_FOREVER);
    HostUpdate_Status = HostUpdate_Core.status;
    if (result != 0) {
        HostUpdate_Status.error = result;
        if (!HostUpdate_Status.active) {
            HostUpdate_Status.state = HOST_STM_UPDATE_FAILED;
        }
    }
    HostUpdate_BootOwned = HostStmUpdate_UsesBootTransport(&HostUpdate_Core);
#if defined(MULTI_MCU_PHASE12_METRICS)
    HostUpdate_StopTiming(&HostUpdate_JobTiming, HostUpdate_Status.active);
#endif
    HostUpdate_Queued = false;
    HostUpdate_Processing = false;
    k_mutex_unlock(&HostUpdate_Lock);
}

static bool HostUpdate_Uint(zcbor_state_t *state, const char *key, uint32_t value) {
    return zcbor_tstr_put_term(state, key, 32U) && zcbor_uint32_put(state, value);
}
static int HostUpdate_Error(zcbor_state_t *state, const char *error) {
    return zcbor_tstr_put_lit(state, "error") && zcbor_tstr_put_term(state, error, 32U)
               ? MGMT_ERR_EOK
               : MGMT_ERR_EMSGSIZE;
}
static int HostUpdate_Reply(zcbor_state_t *state, uint64_t request, uint64_t job) {
    return zcbor_tstr_put_lit(state, "request_id") && zcbor_uint64_put(state, request) &&
                   zcbor_tstr_put_lit(state, "job") && zcbor_uint64_put(state, job)
               ? MGMT_ERR_EOK
               : MGMT_ERR_EMSGSIZE;
}
int HostUpdateAdapter_StatusRead(struct smp_streamer *stream) {
    zcbor_state_t *state = stream->writer->zs;
    k_mutex_lock(&HostUpdate_Lock, K_FOREVER);
    const HostStmUpdate_Status *status = &HostUpdate_Status;
    bool ok = HostUpdate_Uint(state, "host_boot_id", HostUpdate_BootId) &&
              zcbor_tstr_put_lit(state, "job") && zcbor_uint64_put(state, HostUpdate_Job) &&
              zcbor_tstr_put_lit(state, "state") &&
              zcbor_tstr_put_term(state, HostStmUpdate_StateName(status->state), 32U) &&
              HostUpdate_Uint(state, "offset", status->offset) &&
              HostUpdate_Uint(state, "total", status->total) &&
              HostUpdate_Uint(state, "slot", status->slot) &&
              HostUpdate_Uint(state, "confirmed_slot", status->confirmed_slot) &&
              HostUpdate_Uint(state, "boot_state", status->boot_state) &&
              HostUpdate_Uint(state, "protocol", status->protocol) &&
              zcbor_tstr_put_lit(state, "error") && zcbor_int32_put(state, status->error) &&
              zcbor_tstr_put_lit(state, "active") &&
              zcbor_bool_put(state, status->active || HostUpdate_Queued);
#if defined(MULTI_MCU_PHASE12_METRICS)
    ok = ok && HostUpdate_EncodeTiming(state, &HostUpdate_JobTiming, status->offset);
#endif
    k_mutex_unlock(&HostUpdate_Lock);
    return ok ? MGMT_ERR_EOK : MGMT_ERR_EMSGSIZE;
}

static bool HostUpdate_TextEquals(const struct zcbor_string *value, const char *text) {
    return value->len == strlen(text) && memcmp(value->value, text, value->len) == 0;
}
static int HostUpdate_Write(struct smp_streamer *stream, bool reset) {
    HostUpdate_Request request = {0};
    uint32_t expected_boot = 0;
    struct zcbor_string action = {0}, header = {0}, data = {0};
    size_t decoded;
    struct zcbor_map_decode_key_val keys[] = {
        ZCBOR_MAP_DECODE_KEY_DECODER("request_id", zcbor_uint64_decode, &request.request),
        ZCBOR_MAP_DECODE_KEY_DECODER("expected_boot_id", zcbor_uint32_decode, &expected_boot),
        ZCBOR_MAP_DECODE_KEY_DECODER("action", zcbor_tstr_decode, &action),
        ZCBOR_MAP_DECODE_KEY_DECODER("job", zcbor_uint64_decode, &request.job),
        ZCBOR_MAP_DECODE_KEY_DECODER("total", zcbor_uint32_decode, &request.total),
        ZCBOR_MAP_DECODE_KEY_DECODER("offset", zcbor_uint32_decode, &request.offset),
        ZCBOR_MAP_DECODE_KEY_DECODER("header", zcbor_bstr_decode, &header),
        ZCBOR_MAP_DECODE_KEY_DECODER("data", zcbor_bstr_decode, &data),
    };
    if (zcbor_map_decode_bulk(stream->reader->zs, keys, ARRAY_SIZE(keys), &decoded) != 0 ||
        request.request == 0 || !keys[1].found || action.len == 0) {
        return MGMT_ERR_EINVAL;
    }
    if (reset && HostUpdate_TextEquals(&action, "reset")) {
        request.action = HOST_UPDATE_RESET;
    } else if (!reset && HostUpdate_TextEquals(&action, "begin") && header.len == 512 &&
               request.total > 512) {
        request.action = HOST_UPDATE_BEGIN;
        request.length = header.len;
        memcpy(request.data, header.value, header.len);
    } else if (!reset && HostUpdate_TextEquals(&action, "chunk") && data.len > 0 &&
               data.len <= 256) {
        request.action = HOST_UPDATE_CHUNK;
        request.length = data.len;
        memcpy(request.data, data.value, data.len);
    } else if (!reset && HostUpdate_TextEquals(&action, "finish")) {
        request.action = HOST_UPDATE_FINISH;
    } else if (!reset && HostUpdate_TextEquals(&action, "abort")) {
        request.action = HOST_UPDATE_ABORT;
    } else {
        return MGMT_ERR_EINVAL;
    }
    bool shape = false;
    switch (request.action) {
    case HOST_UPDATE_RESET:
        shape = decoded == 3;
        break;
    case HOST_UPDATE_BEGIN:
        shape = decoded == 5 && keys[4].found && keys[6].found;
        break;
    case HOST_UPDATE_CHUNK:
        shape = decoded == 6 && keys[3].found && keys[5].found && keys[7].found;
        break;
    case HOST_UPDATE_FINISH:
    case HOST_UPDATE_ABORT:
        shape = decoded == 4 && keys[3].found;
        break;
    default:
        break;
    }
    if (!shape) {
        return MGMT_ERR_EINVAL;
    }
    zcbor_state_t *state = stream->writer->zs;
    k_mutex_lock(&HostUpdate_Lock, K_FOREVER);
    int result;
    if (expected_boot != HostUpdate_BootId || expected_boot == 0) {
        result = HostUpdate_Error(state, "host_restarted");
        goto done;
    }
    if (!HostUpdate_Available) {
        result = HostUpdate_Error(state, "unsupported");
        goto done;
    }
    for (size_t i = 0; i <= HOST_UPDATE_CACHE_SIZE; i++) {
        const HostUpdate_Request *cached =
            i == HOST_UPDATE_CACHE_SIZE ? &HostUpdate_StartRequest : &HostUpdate_Cache[i];
        if (cached->request == request.request) {
            /* Compare exact bytes, not a lossy checksum. */
            bool same = cached->action == request.action && cached->offset == request.offset &&
                        cached->total == request.total && cached->length == request.length &&
                        (request.action == HOST_UPDATE_BEGIN ||
                         request.action == HOST_UPDATE_RESET || cached->job == request.job) &&
                        memcmp(cached->data, request.data, request.length) == 0;
            result = same ? HostUpdate_Reply(state, request.request, cached->job)
                          : HostUpdate_Error(state, "request_conflict");
            goto done;
        }
    }
    if (HostUpdate_Queued || HostUpdate_Processing || HostRom_IsBusy()) {
        result = HostUpdate_Error(state, "busy");
        goto done;
    }
    if (request.action == HOST_UPDATE_BEGIN || request.action == HOST_UPDATE_RESET) {
        if (HostUpdate_Status.active || HostUpdate_RuntimeBusy) {
            result = HostUpdate_Error(state, "busy");
            goto done;
        }
        if (request.action == HOST_UPDATE_BEGIN) {
            FirmwareImage_SignedManifest manifest;
            if (FirmwareImage_ParseSignedPrefix(request.data, 512, request.total, &manifest) !=
                    FIRMWARE_IMAGE_STATUS_OK ||
                manifest.manifest.target_id != FIRMWARE_IMAGE_TARGET_STM32U083_APPLICATION ||
                manifest.manifest.hardware_id != FIRMWARE_IMAGE_HARDWARE_NUCLEO_U083RC ||
                manifest.slot_id > 1 ||
                manifest.manifest.load_address !=
                    (manifest.slot_id == 0 ? FIRMWARE_IMAGE_STM32_SLOT0_VECTOR_ADDRESS
                                           : FIRMWARE_IMAGE_STM32_SLOT1_VECTOR_ADDRESS) ||
                manifest.manifest.payload_size > FIRMWARE_IMAGE_STM32_MAX_PAYLOAD_SIZE ||
                manifest.manifest.required_wire_major != 1 ||
                manifest.manifest.required_wire_minor != 0 ||
                manifest.manifest.required_management_major != 1 ||
                manifest.manifest.required_management_minor != 0) {
                result = HostUpdate_Error(state, "invalid_image");
                goto done;
            }
        }
        request.job = request.request;
        HostUpdate_Job = request.job;
#if defined(MULTI_MCU_PHASE12_METRICS)
        HostUpdate_StartTiming(&HostUpdate_JobTiming);
#endif
        HostUpdate_ResetJob = reset;
        HostUpdate_StartRequest = request;
        HostUpdate_Status = (HostStmUpdate_Status){.state = HOST_STM_UPDATE_QUERYING,
                                                   .total = request.total,
                                                   .slot = reset ? 255 : request.data[42],
                                                   .confirmed_slot = 255,
                                                   .active = true};
    } else {
        if (request.job != HostUpdate_Job || request.job == 0) {
            result = HostUpdate_Error(state, "wrong_job");
            goto done;
        }
        bool valid = !HostUpdate_ResetJob && HostUpdate_Status.active &&
                     ((request.action == HOST_UPDATE_CHUNK &&
                       HostUpdate_Status.state == HOST_STM_UPDATE_READY &&
                       request.offset == HostUpdate_Status.offset &&
                       request.length <= HostUpdate_Status.total - HostUpdate_Status.offset) ||
                      (request.action == HOST_UPDATE_FINISH &&
                       HostUpdate_Status.state == HOST_STM_UPDATE_READY &&
                       HostUpdate_Status.offset == HostUpdate_Status.total) ||
                      (request.action == HOST_UPDATE_ABORT &&
                       HostUpdate_Status.state < HOST_STM_UPDATE_PENDING));
        if (!valid) {
            result = HostUpdate_Error(state, "invalid_state");
            goto done;
        }
        if (request.action == HOST_UPDATE_CHUNK && request.offset < 512U) {
            size_t header_length = 512U - request.offset;
            if (header_length > request.length) {
                header_length = request.length;
            }
            if (memcmp(request.data, HostUpdate_Core.header + request.offset, header_length) != 0) {
                result = HostUpdate_Error(state, "invalid_image");
                goto done;
            }
        }
    }
    HostUpdate_Mailbox = request;
    HostUpdate_Queued = true;
    HostUpdate_Cache[HostUpdate_CacheNext] = request;
    HostUpdate_CacheNext = (HostUpdate_CacheNext + 1) % HOST_UPDATE_CACHE_SIZE;
    result = HostUpdate_Reply(state, request.request, request.job);
done:
    k_mutex_unlock(&HostUpdate_Lock);
    return result;
}
int HostUpdateAdapter_ResetWrite(struct smp_streamer *stream) {
    return HostUpdate_Write(stream, true);
}
int HostUpdateAdapter_UpdateWrite(struct smp_streamer *stream) {
    return HostUpdate_Write(stream, false);
}

bool HostUpdateAdapter_RomAvailable(void) {
#if defined(MULTI_MCU_PHASE11_ROM)
    return HostUpdate_Available;
#else
    return false;
#endif
}

int HostUpdateAdapter_RomStatusRead(struct smp_streamer *stream) {
#if defined(MULTI_MCU_PHASE11_ROM)
    zcbor_state_t *state = stream->writer->zs;
    k_mutex_lock(&HostUpdate_Lock, K_FOREVER);
    const HostStmUpdate_Status *status = &HostRom_Status;
    bool ok = HostUpdate_Uint(state, "host_boot_id", HostUpdate_BootId) &&
              zcbor_tstr_put_lit(state, "job") && zcbor_uint64_put(state, HostRom_Job) &&
              zcbor_tstr_put_lit(state, "state") &&
              zcbor_tstr_put_term(state, HostStmUpdate_StateName(status->state), 32) &&
              HostUpdate_Uint(state, "offset", status->offset) &&
              HostUpdate_Uint(state, "total", status->total) &&
              HostUpdate_Uint(state, "slot", 255) &&
              HostUpdate_Uint(state, "confirmed_slot", 255) &&
              HostUpdate_Uint(state, "boot_state", 0) && HostUpdate_Uint(state, "protocol", 0) &&
              HostUpdate_Uint(state, "device_id", HostRom_DeviceId) &&
              HostUpdate_Uint(state, "rom_version", HostRom_Version) &&
              zcbor_tstr_put_lit(state, "error") && zcbor_int32_put(state, status->error) &&
              zcbor_tstr_put_lit(state, "active") && zcbor_bool_put(state, status->active);
#if defined(MULTI_MCU_PHASE12_METRICS)
    ok = ok && HostUpdate_EncodeTiming(state, &HostRom_JobTiming, status->offset);
#endif
    k_mutex_unlock(&HostUpdate_Lock);
    return ok ? MGMT_ERR_EOK : MGMT_ERR_EMSGSIZE;
#else
    return HostUpdate_Error(stream->writer->zs, "unsupported");
#endif
}

int HostUpdateAdapter_RomWrite(struct smp_streamer *stream) {
#if defined(MULTI_MCU_PHASE11_ROM)
    HostUpdate_Request request = {0};
    uint32_t expected_boot = 0;
    bool prerequisites = false;
    struct zcbor_string action = {0}, sha = {0}, data = {0};
    size_t decoded;
    struct zcbor_map_decode_key_val keys[] = {
        ZCBOR_MAP_DECODE_KEY_DECODER("request_id", zcbor_uint64_decode, &request.request),
        ZCBOR_MAP_DECODE_KEY_DECODER("expected_boot_id", zcbor_uint32_decode, &expected_boot),
        ZCBOR_MAP_DECODE_KEY_DECODER("action", zcbor_tstr_decode, &action),
        ZCBOR_MAP_DECODE_KEY_DECODER("job", zcbor_uint64_decode, &request.job),
        ZCBOR_MAP_DECODE_KEY_DECODER("total", zcbor_uint32_decode, &request.total),
        ZCBOR_MAP_DECODE_KEY_DECODER("offset", zcbor_uint32_decode, &request.offset),
        ZCBOR_MAP_DECODE_KEY_DECODER("sha256", zcbor_bstr_decode, &sha),
        ZCBOR_MAP_DECODE_KEY_DECODER("data", zcbor_bstr_decode, &data),
        ZCBOR_MAP_DECODE_KEY_DECODER("prerequisites", zcbor_bool_decode, &prerequisites),
    };
    if (zcbor_map_decode_bulk(stream->reader->zs, keys, ARRAY_SIZE(keys), &decoded) != 0 ||
        request.request == 0 || !keys[1].found || action.len == 0) {
        return MGMT_ERR_EINVAL;
    }
    if (HostUpdate_TextEquals(&action, "begin") && decoded == 6 && keys[4].found && keys[6].found &&
        keys[8].found && prerequisites && sha.len == 32 && request.total >= 8 &&
        request.total <= STM32_ROM_BOOT_SIZE) {
        request.action = HOST_UPDATE_ROM_BEGIN;
        request.length = 32;
        memcpy(request.data, sha.value, 32);
    } else if (HostUpdate_TextEquals(&action, "chunk") && decoded == 6 && keys[3].found &&
               keys[5].found && keys[7].found && data.len > 0 && data.len <= 256) {
        request.action = HOST_UPDATE_ROM_CHUNK;
        request.length = data.len;
        memcpy(request.data, data.value, data.len);
    } else if (HostUpdate_TextEquals(&action, "finish") && decoded == 4 && keys[3].found) {
        request.action = HOST_UPDATE_ROM_FINISH;
    } else if (HostUpdate_TextEquals(&action, "abort") && decoded == 4 && keys[3].found) {
        request.action = HOST_UPDATE_ROM_ABORT;
    } else {
        return MGMT_ERR_EINVAL;
    }
    zcbor_state_t *state = stream->writer->zs;
    k_mutex_lock(&HostUpdate_Lock, K_FOREVER);
    int result;
    if (expected_boot != HostUpdate_BootId || expected_boot == 0) {
        result = HostUpdate_Error(state, "host_restarted");
        goto done;
    }
    if (!HostUpdate_Available) {
        result = HostUpdate_Error(state, "unsupported");
        goto done;
    }
    for (size_t i = 0; i <= HOST_UPDATE_CACHE_SIZE; ++i) {
        const HostUpdate_Request *cached =
            i == HOST_UPDATE_CACHE_SIZE ? &HostUpdate_StartRequest : &HostUpdate_Cache[i];
        if (cached->request == request.request) {
            bool same = cached->action == request.action && cached->offset == request.offset &&
                        cached->total == request.total && cached->length == request.length &&
                        (request.action == HOST_UPDATE_ROM_BEGIN || cached->job == request.job) &&
                        memcmp(cached->data, request.data, request.length) == 0;
            result = same ? HostUpdate_Reply(state, request.request, cached->job)
                          : HostUpdate_Error(state, "request_conflict");
            goto done;
        }
    }
    if (HostUpdate_Queued || HostUpdate_Processing || HostUpdate_Status.active) {
        result = HostUpdate_Error(state, "busy");
        goto done;
    }
    if (request.action == HOST_UPDATE_ROM_BEGIN) {
        if (HostRom_Status.active || HostUpdate_RuntimeBusy) {
            result = HostUpdate_Error(state, "busy");
            goto done;
        }
        request.job = request.request;
        HostRom_Job = request.job;
#if defined(MULTI_MCU_PHASE12_METRICS)
        HostUpdate_StartTiming(&HostRom_JobTiming);
#endif
        HostUpdate_StartRequest = request;
        HostRom_Status = (HostStmUpdate_Status){.state = HOST_STM_UPDATE_QUERYING,
                                                .total = request.total,
                                                .slot = 255,
                                                .confirmed_slot = 255,
                                                .active = true};
        HostRom_Activity = k_uptime_get();
        HostRom_DeviceId = 0;
        HostRom_Version = 0;
    } else {
        if (request.job == 0 || request.job != HostRom_Job) {
            result = HostUpdate_Error(state, "wrong_job");
            goto done;
        }
        bool valid = HostRom_Status.active &&
                     ((request.action == HOST_UPDATE_ROM_CHUNK &&
                       HostRom_Status.state == HOST_STM_UPDATE_READY &&
                       request.offset == HostRom_Status.offset && request.offset % 8U == 0 &&
                       request.length <= HostRom_Status.total - HostRom_Status.offset &&
                       (request.length % 8U == 0 ||
                        request.offset + request.length == HostRom_Status.total)) ||
                      (request.action == HOST_UPDATE_ROM_FINISH &&
                       HostRom_Status.state == HOST_STM_UPDATE_READY &&
                       HostRom_Status.offset == HostRom_Status.total) ||
                      request.action == HOST_UPDATE_ROM_ABORT);
        if (!valid) {
            result = HostUpdate_Error(state, "invalid_state");
            goto done;
        }
    }
    HostUpdate_Mailbox = request;
    if (request.action == HOST_UPDATE_ROM_CHUNK) {
        HostRom_Status.state = HOST_STM_UPDATE_WRITING;
    } else if (request.action == HOST_UPDATE_ROM_FINISH) {
        HostRom_Status.state = HOST_STM_UPDATE_FINALIZING;
    }
    HostUpdate_Queued = true;
    HostUpdate_Cache[HostUpdate_CacheNext] = request;
    HostUpdate_CacheNext = (HostUpdate_CacheNext + 1) % HOST_UPDATE_CACHE_SIZE;
    result = HostUpdate_Reply(state, request.request, request.job);
done:
    k_mutex_unlock(&HostUpdate_Lock);
    return result;
#else
    return HostUpdate_Error(stream->writer->zs, "unsupported");
#endif
}
