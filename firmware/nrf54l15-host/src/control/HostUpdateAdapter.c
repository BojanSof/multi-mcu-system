#include "HostUpdateAdapter.h"
#include "HostNodeManager.h"
#include "HostStmUpdate.h"
#include "HostTransport.h"
#include "firmware_image/FirmwareImage.h"
#include "node_api/NodeCommands.h"
#include "protocol/ProtocolTypes.h"
#include <errno.h>
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
    HOST_UPDATE_ABORT
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
}

bool HostUpdateAdapter_IsAvailable(void) { return HostUpdate_Available; }
bool HostUpdateAdapter_ReserveRuntime(void) {
    k_mutex_lock(&HostUpdate_Lock, K_FOREVER);
    bool admitted = !HostUpdate_Queued && !HostUpdate_Status.active;
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
    bool busy = HostUpdate_Queued || HostUpdate_Status.active;
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
        }
    }
    HostStmUpdate_Process(&HostUpdate_Core);
    HostUpdate_Status = HostUpdate_Core.status;
    if (result != 0) {
        HostUpdate_Status.error = result;
        if (!HostUpdate_Status.active) {
            HostUpdate_Status.state = HOST_STM_UPDATE_FAILED;
        }
    }
    HostUpdate_BootOwned = HostStmUpdate_UsesBootTransport(&HostUpdate_Core);
    HostUpdate_Queued = false;
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
    if (HostUpdate_Queued) {
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
