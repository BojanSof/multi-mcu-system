#include "HostControl.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#if defined(HOST_CONTROL_NATIVE_TEST)
#include "HostControlTestSdk.h"
#else
#include <zcbor_decode.h>
#include <zcbor_encode.h>

#include <mgmt/mcumgr/util/zcbor_bulk.h>
#include <zephyr/kernel.h>
#include <zephyr/mgmt/mcumgr/mgmt/handlers.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/mgmt/mcumgr/smp/smp.h>
#include <zephyr/random/random.h>
#endif

#include "HostNodeManager.h"
#if defined(MULTI_MCU_RELIABILITY)
#include "HostReliability.h"
#endif
#if defined(MULTI_MCU_PHASE12_METRICS)
#include "HostTransport.h"
#endif
#if defined(MULTI_MCU_PHASE10_UPDATE)
#include "HostUpdateAdapter.h"
#endif
#include "virtual_io/VirtualIo.h"

#define HOST_CONTROL_GROUP 64U
#define HOST_CONTROL_CACHE_SIZE 8U
#define HOST_CONTROL_HISTORY_SIZE 16U
#define HOST_CONTROL_PAGE_SIZE 4U

typedef struct {
    uint64_t request;
    uint32_t operation;
    uint32_t session;
    uint16_t service;
    uint16_t command;
    size_t length;
    uint8_t data[NODE_API_SERVICE_DATA_MAX_LENGTH];
    bool pending;
} HostControl_Call;

typedef struct {
    uint32_t id;
    uint32_t time;
    uint32_t session;
    uint32_t operation;
    uint32_t status;
    uint16_t service;
    uint16_t code;
    HostNodeManager_RecordKind kind;
    size_t length;
    uint8_t data[NODE_API_SERVICE_DATA_MAX_LENGTH];
} HostControl_Record;

static K_MUTEX_DEFINE(HostControl_Lock);
static HostNodeManager_Status HostControl_Status;
static HostControl_Call HostControl_Cache[HOST_CONTROL_CACHE_SIZE];
static HostControl_Call HostControl_Mailbox;
static HostControl_Call HostControl_AsyncActuator;
static HostControl_Record HostControl_History[HOST_CONTROL_HISTORY_SIZE];
static size_t HostControl_CacheNext;
static size_t HostControl_Count;
static size_t HostControl_HistoryHead;
static uint32_t HostControl_Latest;
static uint32_t HostControl_Dropped;
static uint32_t HostControl_NextOperation;
static bool HostControl_Queued;
static bool HostControl_Busy;
static uint32_t HostControl_BootId;
#if defined(MULTI_MCU_RELIABILITY)
static HostReliability_Status HostControl_Health;
static bool HostControl_ClearQueued;
static uint64_t HostControl_ClearRequests[HOST_CONTROL_CACHE_SIZE];
static size_t HostControl_ClearNext;
#endif
#if defined(MULTI_MCU_PHASE12_METRICS)
static HostNodeManager_Metrics HostControl_Metrics;
static HostTransport_Stats HostControl_TransportMetrics;
static uint64_t HostControl_Uptime;
static uint32_t HostControl_MainFree = UINT32_MAX;
static uint32_t HostControl_WorkFree = UINT32_MAX;
static bool HostControl_StackSupported;

static void HostControl_SampleMetrics(void) {
    HostNodeManager_Metrics metrics = HostNodeManager_GetMetrics();
    HostTransport_Stats transport = HostTransport_GetStats();
    uint32_t main_free = UINT32_MAX, work_free = UINT32_MAX;
    bool supported = false;
#if !defined(HOST_CONTROL_NATIVE_TEST) && defined(CONFIG_INIT_STACKS) &&                           \
    defined(CONFIG_THREAD_STACK_INFO)
    size_t main_unused, work_unused;
    if (k_thread_stack_space_get(k_current_get(), &main_unused) == 0 &&
        k_thread_stack_space_get(&k_sys_work_q.thread, &work_unused) == 0) {
        main_free = (uint32_t)main_unused;
        work_free = (uint32_t)work_unused;
        supported = true;
    }
#endif
    k_mutex_lock(&HostControl_Lock, K_FOREVER);
    HostControl_Metrics = metrics;
    HostControl_TransportMetrics = transport;
    HostControl_Uptime = (uint64_t)k_uptime_get();
    HostControl_MainFree = main_free;
    HostControl_WorkFree = work_free;
    HostControl_StackSupported = supported;
    k_mutex_unlock(&HostControl_Lock);
}

static bool HostControl_Uint64(zcbor_state_t *state, const char *key, uint64_t value) {
    return zcbor_tstr_put_term(state, key, 32U) && zcbor_uint64_put(state, value);
}
#endif

static bool HostControl_Uint(zcbor_state_t *state, const char *key, uint32_t value) {
    return zcbor_tstr_put_term(state, key, 32U) && zcbor_uint32_put(state, value);
}

static bool HostControl_Text(zcbor_state_t *state, const char *key, const char *value) {
    return zcbor_tstr_put_term(state, key, 32U) && zcbor_tstr_put_term(state, value, 64U);
}

static int HostControl_Error(zcbor_state_t *state, const char *error) {
    return HostControl_Text(state, "error", error) ? MGMT_ERR_EOK : MGMT_ERR_EMSGSIZE;
}

static void HostControl_RecordReceived(HostNodeManager_RecordKind kind, uint32_t session,
                                       uint32_t operation, uint16_t service, uint16_t code,
                                       uint32_t status, const uint8_t *data, size_t length) {
    if (length > NODE_API_SERVICE_DATA_MAX_LENGTH) {
        return;
    }
    k_mutex_lock(&HostControl_Lock, K_FOREVER);
    if (operation != 0U && operation == HostControl_AsyncActuator.operation &&
        (kind == HOST_NODE_RECORD_ERROR ||
         (kind == HOST_NODE_RECORD_EVENT && service == VIRTUAL_IO_ACTUATOR_SERVICE_ID &&
          code == VIRTUAL_IO_ACTUATOR_EVENT_COMPLETE))) {
        HostControl_AsyncActuator.pending = false;
    }
    for (size_t index = 0U; index < HOST_CONTROL_CACHE_SIZE; ++index) {
        HostControl_Call *call = &HostControl_Cache[index];
        if (operation != 0U && call->operation == operation && kind == HOST_NODE_RECORD_RESPONSE &&
            service == VIRTUAL_IO_ACTUATOR_SERVICE_ID &&
            code == VIRTUAL_IO_ACTUATOR_COMMAND_START) {
            HostControl_AsyncActuator = *call;
        }
        if (call->operation == operation && operation != 0U &&
            (kind == HOST_NODE_RECORD_ERROR ||
             (kind == HOST_NODE_RECORD_RESPONSE && !(service == VIRTUAL_IO_ACTUATOR_SERVICE_ID &&
                                                     code == VIRTUAL_IO_ACTUATOR_COMMAND_START)) ||
             (kind == HOST_NODE_RECORD_EVENT && service == VIRTUAL_IO_ACTUATOR_SERVICE_ID &&
              code == VIRTUAL_IO_ACTUATOR_EVENT_COMPLETE))) {
            call->pending = false;
        }
    }
    HostControl_Latest++;
    if (HostControl_Latest == 0U) {
        HostControl_Latest = 1U;
    }
    HostControl_Record *record = &HostControl_History[HostControl_HistoryHead];
    HostControl_HistoryHead = (HostControl_HistoryHead + 1U) % HOST_CONTROL_HISTORY_SIZE;
    *record = (HostControl_Record){.id = HostControl_Latest,
                                   .time = (uint32_t)k_uptime_get(),
                                   .session = session,
                                   .operation = operation,
                                   .service = service,
                                   .code = code,
                                   .status = status,
                                   .kind = kind,
                                   .length = length};
    if (length != 0U) {
        memcpy(record->data, data, length);
    }
    if (HostControl_Count == HOST_CONTROL_HISTORY_SIZE) {
        HostControl_Dropped++;
    } else {
        HostControl_Count++;
    }
    k_mutex_unlock(&HostControl_Lock);
}

void HostControl_Initialize(void) {
    do {
        HostControl_BootId = sys_rand32_get();
    } while (HostControl_BootId == 0U);
#if defined(MULTI_MCU_PHASE10_UPDATE)
    HostUpdateAdapter_Initialize(HostControl_BootId);
#endif
    HostNodeManager_SetRecordCallback(HostControl_RecordReceived);
    HostControl_Process();
}

void HostControl_Process(void) {
#if defined(MULTI_MCU_RELIABILITY)
    k_mutex_lock(&HostControl_Lock, K_FOREVER);
    const bool clear = HostControl_ClearQueued;
    HostControl_ClearQueued = false;
    k_mutex_unlock(&HostControl_Lock);
    if (clear) {
        HostReliability_Clear();
    }
#endif
    HostControl_Call call;
    bool queued;
    const HostNodeManager_Status current = HostNodeManager_GetStatus();

    /* The actuator may outlive the eight-entry request cache while other reads run. */
    k_mutex_lock(&HostControl_Lock, K_FOREVER);
    HostControl_Call active = HostControl_AsyncActuator;
    k_mutex_unlock(&HostControl_Lock);
    if (active.pending && current.session_generation != active.session) {
        HostControl_RecordReceived(HOST_NODE_RECORD_ERROR, active.session, active.operation,
                                   active.service, active.command, ECANCELED, NULL, 0U);
    }

    /* Discard asynchronous work from the previous node session; never replay it. */
    for (size_t index = 0U; index < HOST_CONTROL_CACHE_SIZE; ++index) {
        k_mutex_lock(&HostControl_Lock, K_FOREVER);
        HostControl_Call previous = HostControl_Cache[index];
        bool queued_call =
            HostControl_Queued && previous.operation == HostControl_Mailbox.operation;
        bool canceled =
            previous.pending && !queued_call && current.session_generation != previous.session;
        k_mutex_unlock(&HostControl_Lock);
        if (canceled) {
            HostControl_RecordReceived(HOST_NODE_RECORD_ERROR, previous.session, previous.operation,
                                       previous.service, previous.command, ECANCELED, NULL, 0U);
        }
    }

    k_mutex_lock(&HostControl_Lock, K_FOREVER);
    queued = HostControl_Queued;
    if (queued) {
        call = HostControl_Mailbox;
        HostControl_Queued = false;
        HostControl_Busy = true;
    }
    k_mutex_unlock(&HostControl_Lock);

    if (queued) {
        const HostNodeManager_Status status = HostNodeManager_GetStatus();
        int result = status.session_generation != call.session
                         ? -ECANCELED
                         : HostNodeManager_CallServiceWithOperation(
                               call.service, call.command, call.data, call.length, call.operation);
        if (result != 0) {
            HostControl_RecordReceived(HOST_NODE_RECORD_ERROR, call.session, call.operation,
                                       call.service, call.command, (uint32_t)-result, NULL, 0U);
        }
    }

    k_mutex_lock(&HostControl_Lock, K_FOREVER);
    HostControl_Status = HostNodeManager_GetStatus();
#if defined(MULTI_MCU_RELIABILITY)
    HostControl_Health = HostReliability_GetStatus();
#endif
    HostControl_Busy = HostNodeManager_IsBusy() || HostControl_Queued;
#if defined(MULTI_MCU_PHASE10_UPDATE)
#if defined(MULTI_MCU_PHASE11_ROM)
    /* A dead node's discovery retries must not prevent its ROM rescue. Keep
     * actual operator work exclusive, but allow recovery to quiesce discovery. */
    bool runtime_work = HostControl_Queued || HostControl_AsyncActuator.pending ||
                        (HostControl_Status.state == HOST_LINK_ONLINE && HostControl_Busy);
    for (size_t i = 0; i < HOST_CONTROL_CACHE_SIZE; ++i) {
        runtime_work = runtime_work || HostControl_Cache[i].pending;
    }
    HostUpdateAdapter_SetRuntimeBusy(runtime_work);
#else
    HostUpdateAdapter_SetRuntimeBusy(HostControl_Busy || HostControl_AsyncActuator.pending);
#endif
#endif
    k_mutex_unlock(&HostControl_Lock);
#if defined(MULTI_MCU_PHASE12_METRICS)
    HostControl_SampleMetrics();
#endif
}

#if defined(MULTI_MCU_RELIABILITY)
static bool HostControl_EncodeHealth(zcbor_state_t *state) {
    return HostControl_Text(state, "health",
                            HostControl_Health.health == NULL ? "degraded"
                                                              : HostControl_Health.health) &&
           HostControl_Text(state, "reason",
                            HostControl_Health.reason == NULL ? "startup"
                                                              : HostControl_Health.reason) &&
           HostControl_Uint(state, "recovery_attempts", HostControl_Health.recovery_attempts) &&
           HostControl_Uint(state, "recovery_total", HostControl_Health.recovery_total) &&
           HostControl_Uint(state, "reset_cause", HostControl_Health.reset_cause) &&
           zcbor_tstr_put_lit(state, "watchdog") &&
           zcbor_bool_put(state, HostControl_Health.watchdog) &&
           zcbor_tstr_put_lit(state, "gpio_interrupts") &&
           zcbor_bool_put(state, HostControl_Health.gpio_interrupts) &&
           zcbor_tstr_put_lit(state, "failed") && zcbor_bool_put(state, HostControl_Health.failed);
}
static int HostControl_RecoveryRead(struct smp_streamer *stream) {
    k_mutex_lock(&HostControl_Lock, K_FOREVER);
    bool ok = HostControl_Uint(stream->writer->zs, "host_boot_id", HostControl_BootId) &&
              HostControl_EncodeHealth(stream->writer->zs);
    k_mutex_unlock(&HostControl_Lock);
    return ok ? MGMT_ERR_EOK : MGMT_ERR_EMSGSIZE;
}
static int HostControl_RecoveryWrite(struct smp_streamer *stream) {
    uint32_t boot = 0U;
    uint64_t request = 0U;
    struct zcbor_string action = {0};
    size_t decoded;
    struct zcbor_map_decode_key_val keys[] = {
        ZCBOR_MAP_DECODE_KEY_DECODER("expected_boot_id", zcbor_uint32_decode, &boot),
        ZCBOR_MAP_DECODE_KEY_DECODER("request_id", zcbor_uint64_decode, &request),
        ZCBOR_MAP_DECODE_KEY_DECODER("action", zcbor_tstr_decode, &action),
    };
    if (zcbor_map_decode_bulk(stream->reader->zs, keys, ARRAY_SIZE(keys), &decoded) != 0 ||
        decoded != 3U || request == 0U || action.len != 5U ||
        memcmp(action.value, "clear", 5U) != 0) {
        return MGMT_ERR_EINVAL;
    }
    zcbor_state_t *state = stream->writer->zs;
    k_mutex_lock(&HostControl_Lock, K_FOREVER);
    int result;
    if (boot != HostControl_BootId) {
        result = HostControl_Error(state, "host_restarted");
    } else {
        bool seen = false;
        for (size_t i = 0U; i < HOST_CONTROL_CACHE_SIZE; ++i) {
            seen = seen || HostControl_ClearRequests[i] == request;
        }
        if (!seen && (HostControl_ClearQueued ||
                      (HostControl_Busy && HostControl_Status.state == HOST_LINK_ONLINE) ||
                      HostControl_AsyncActuator.pending || !HostUpdateAdapter_ReserveRuntime())) {
            result = HostControl_Error(state, "busy");
        } else {
            if (!seen) {
                HostControl_ClearRequests[HostControl_ClearNext] = request;
                HostControl_ClearNext = (HostControl_ClearNext + 1U) % HOST_CONTROL_CACHE_SIZE;
                HostControl_ClearQueued = true;
                HostControl_Busy = true;
            }
            bool ok = zcbor_tstr_put_lit(state, "request_id") && zcbor_uint64_put(state, request) &&
                      zcbor_tstr_put_lit(state, "accepted") && zcbor_bool_put(state, true);
            result = ok ? MGMT_ERR_EOK : MGMT_ERR_EMSGSIZE;
        }
    }
    k_mutex_unlock(&HostControl_Lock);
    return result;
}
#endif

static int HostControl_StatusRead(struct smp_streamer *stream) {
    zcbor_state_t *state = stream->writer->zs;
    char firmware[32];

    k_mutex_lock(&HostControl_Lock, K_FOREVER);
    const HostNodeManager_Status *status = &HostControl_Status;
    bool busy = HostControl_Busy;
#if defined(MULTI_MCU_PHASE10_UPDATE)
    busy = busy || HostUpdateAdapter_IsBusy();
#endif
    if (status->session_generation == 0U ||
        status->node_versions_session != status->session_generation) {
        snprintf(firmware, sizeof(firmware), "unknown");
    } else {
        snprintf(firmware, sizeof(firmware), "%u.%u.%u", status->node_firmware_major,
                 status->node_firmware_minor, status->node_firmware_patch);
    }
    bool ok = HostControl_Uint(state, "api", 1U) &&
              HostControl_Uint(state, "host_boot_id", HostControl_BootId) &&
              HostControl_Text(state, "state", HostNodeManager_StateName(status->state)) &&
              HostControl_Uint(state, "session", status->session_generation) &&
              HostControl_Text(state, "identity", status->identity) &&
              HostControl_Uint(state, "capabilities", status->capabilities) &&
              HostControl_Uint(state, "timeouts", status->timeouts) &&
              HostControl_Uint(state, "rejected", status->rejected_responses) &&
              HostControl_Uint(state, "received_events", status->received_events) &&
              HostControl_Uint(state, "dropped", HostControl_Dropped) &&
              zcbor_tstr_put_lit(state, "busy") && zcbor_bool_put(state, busy) &&
              HostControl_Text(state, "firmware", firmware);
#if defined(MULTI_MCU_RELIABILITY)
    ok = ok && HostControl_EncodeHealth(state);
#endif
#if defined(MULTI_MCU_PHASE12_METRICS)
    ok = ok && zcbor_tstr_put_lit(state, "metrics") && zcbor_bool_put(state, true);
#endif
#if defined(MULTI_MCU_PHASE10_UPDATE)
    ok = ok && zcbor_tstr_put_lit(state, "supervision") &&
         zcbor_bool_put(state, HostUpdateAdapter_IsAvailable()) &&
         zcbor_tstr_put_lit(state, "stm_update") &&
         zcbor_bool_put(state, HostUpdateAdapter_IsAvailable());
#if defined(MULTI_MCU_PHASE11_ROM)
    ok = ok && zcbor_tstr_put_lit(state, "rom_recovery") &&
         zcbor_bool_put(state, HostUpdateAdapter_RomAvailable());
#endif
#endif
    k_mutex_unlock(&HostControl_Lock);
    return ok ? MGMT_ERR_EOK : MGMT_ERR_EMSGSIZE;
}

static int HostControl_ServicesRead(struct smp_streamer *stream) {
    zcbor_state_t *state = stream->writer->zs;
    k_mutex_lock(&HostControl_Lock, K_FOREVER);
    bool ok = zcbor_tstr_put_lit(state, "services") &&
              zcbor_list_start_encode(state, NODE_API_MAX_SERVICES);
    for (size_t index = 0U; ok && index < HostControl_Status.service_count; ++index) {
        const NodeApi_ServiceDescriptor *service = &HostControl_Status.services[index];
        ok = zcbor_map_start_encode(state, 4U) &&
             HostControl_Uint(state, "id", service->service_id) &&
             HostControl_Uint(state, "major", service->version_major) &&
             HostControl_Uint(state, "minor", service->version_minor) &&
             HostControl_Uint(state, "flags", service->flags) && zcbor_map_end_encode(state, 4U);
    }
    ok = ok && zcbor_list_end_encode(state, NODE_API_MAX_SERVICES);
    k_mutex_unlock(&HostControl_Lock);
    return ok ? MGMT_ERR_EOK : MGMT_ERR_EMSGSIZE;
}

static int HostControl_CallResult(zcbor_state_t *state, uint64_t request, uint32_t operation) {
    bool ok = zcbor_tstr_put_lit(state, "request_id") && zcbor_uint64_put(state, request) &&
              HostControl_Uint(state, "operation", operation);
    return ok ? MGMT_ERR_EOK : MGMT_ERR_EMSGSIZE;
}

static int HostControl_CallWrite(struct smp_streamer *stream) {
    uint64_t request = 0U;
    uint32_t expected_boot = 0U;
    uint32_t service = UINT32_MAX;
    uint32_t command = UINT32_MAX;
    struct zcbor_string data = {0};
    size_t decoded;
    struct zcbor_map_decode_key_val keys[] = {
        ZCBOR_MAP_DECODE_KEY_DECODER("request_id", zcbor_uint64_decode, &request),
        ZCBOR_MAP_DECODE_KEY_DECODER("expected_boot_id", zcbor_uint32_decode, &expected_boot),
        ZCBOR_MAP_DECODE_KEY_DECODER("service", zcbor_uint32_decode, &service),
        ZCBOR_MAP_DECODE_KEY_DECODER("command", zcbor_uint32_decode, &command),
        ZCBOR_MAP_DECODE_KEY_DECODER("data", zcbor_bstr_decode, &data),
    };
    if (zcbor_map_decode_bulk(stream->reader->zs, keys, ARRAY_SIZE(keys), &decoded) != 0 ||
        decoded != ARRAY_SIZE(keys) || request == 0U || service > UINT16_MAX ||
        command > UINT16_MAX || data.len > NODE_API_SERVICE_DATA_MAX_LENGTH) {
        return MGMT_ERR_EINVAL;
    }

    zcbor_state_t *state = stream->writer->zs;
    int result;
    k_mutex_lock(&HostControl_Lock, K_FOREVER);
    if (expected_boot != HostControl_BootId || HostControl_BootId == 0U) {
        result = HostControl_Error(state, "host_restarted");
        goto done;
    }
    for (size_t index = 0U; index < HOST_CONTROL_CACHE_SIZE; ++index) {
        const HostControl_Call *cached = &HostControl_Cache[index];
        if (cached->request == request) {
            bool identical = cached->service == service && cached->command == command &&
                             cached->length == data.len &&
                             (data.len == 0U || memcmp(cached->data, data.value, data.len) == 0);
            result = identical ? HostControl_CallResult(state, request, cached->operation)
                               : HostControl_Error(state, "request_conflict");
            goto done;
        }
    }
    if (HostControl_Status.state != HOST_LINK_ONLINE) {
        result = HostControl_Error(state, "offline");
        goto done;
    }
    bool found = false;
    for (size_t index = 0U; index < HostControl_Status.service_count; ++index) {
        found = found || HostControl_Status.services[index].service_id == service;
    }
    if (!found) {
        result = HostControl_Error(state, "unsupported");
        goto done;
    }
    if (HostControl_Busy || HostControl_Queued
#if defined(MULTI_MCU_PHASE10_UPDATE)
        || !HostUpdateAdapter_ReserveRuntime()
#endif
    ) {
        result = HostControl_Error(state, "busy");
        goto done;
    }
    HostControl_NextOperation++;
    if (HostControl_NextOperation == 0U) {
        HostControl_NextOperation = 1U;
    }
    HostControl_Mailbox = (HostControl_Call){.request = request,
                                             .operation = HostControl_NextOperation,
                                             .session = HostControl_Status.session_generation,
                                             .service = (uint16_t)service,
                                             .command = (uint16_t)command,
                                             .length = data.len,
                                             .pending = true};
    if (data.len != 0U) {
        memcpy(HostControl_Mailbox.data, data.value, data.len);
    }
    HostControl_Cache[HostControl_CacheNext] = HostControl_Mailbox;
    HostControl_CacheNext = (HostControl_CacheNext + 1U) % HOST_CONTROL_CACHE_SIZE;
    HostControl_Queued = true;
    HostControl_Busy = true;
    result = HostControl_CallResult(state, request, HostControl_Mailbox.operation);
done:
    k_mutex_unlock(&HostControl_Lock);
    return result;
}

static int HostControl_EventsRead(struct smp_streamer *stream) {
    uint32_t after = 0U;
    size_t decoded;
    struct zcbor_map_decode_key_val keys[] = {
        ZCBOR_MAP_DECODE_KEY_DECODER("after", zcbor_uint32_decode, &after),
    };
    if (zcbor_map_decode_bulk(stream->reader->zs, keys, ARRAY_SIZE(keys), &decoded) != 0) {
        return MGMT_ERR_EINVAL;
    }
    zcbor_state_t *state = stream->writer->zs;
    size_t emitted = 0U;
    static const char *const kinds[] = {"response", "event", "error"};

    k_mutex_lock(&HostControl_Lock, K_FOREVER);
    bool ok = zcbor_tstr_put_lit(state, "events") &&
              zcbor_list_start_encode(state, HOST_CONTROL_PAGE_SIZE);
    for (size_t index = 0U; ok && index < HostControl_Count && emitted < HOST_CONTROL_PAGE_SIZE;
         ++index) {
        size_t slot =
            (HostControl_HistoryHead + HOST_CONTROL_HISTORY_SIZE - HostControl_Count + index) %
            HOST_CONTROL_HISTORY_SIZE;
        const HostControl_Record *record = &HostControl_History[slot];
        if (after != 0U && (int32_t)(record->id - after) <= 0) {
            continue;
        }
        struct zcbor_string bytes = {.value = record->data, .len = record->length};
        ok = zcbor_map_start_encode(state, 10U) && HostControl_Uint(state, "id", record->id) &&
             HostControl_Uint(state, "time_ms", record->time) &&
             HostControl_Uint(state, "session", record->session) &&
             HostControl_Uint(state, "operation", record->operation) &&
             HostControl_Uint(state, "service", record->service) &&
             HostControl_Uint(state, "code", record->code) &&
             HostControl_Text(state, "kind", kinds[record->kind]) &&
             HostControl_Uint(state, "status", record->status) &&
             zcbor_tstr_put_lit(state, "data") && zcbor_bstr_encode(state, &bytes) &&
             zcbor_map_end_encode(state, 10U);
        emitted++;
    }
    ok = ok && zcbor_list_end_encode(state, HOST_CONTROL_PAGE_SIZE) &&
         HostControl_Uint(state, "dropped", HostControl_Dropped) &&
         HostControl_Uint(state, "host_boot_id", HostControl_BootId) &&
         HostControl_Uint(state, "latest", HostControl_Latest);
    k_mutex_unlock(&HostControl_Lock);
    return ok ? MGMT_ERR_EOK : MGMT_ERR_EMSGSIZE;
}

#if !defined(MULTI_MCU_RELIABILITY) || !defined(MULTI_MCU_PHASE12_METRICS) ||                      \
    !defined(MULTI_MCU_PHASE11_ROM) || !defined(MULTI_MCU_PHASE10_UPDATE)
static int HostControl_Unsupported(struct smp_streamer *stream) {
    return HostControl_Error(stream->writer->zs, "unsupported");
}
#endif

#if defined(MULTI_MCU_PHASE12_METRICS)
static int HostControl_MetricsRead(struct smp_streamer *stream) {
    zcbor_state_t *state = stream->writer->zs;
    k_mutex_lock(&HostControl_Lock, K_FOREVER);
    const HostNodeManager_Metrics *m = &HostControl_Metrics;
    const HostTransport_Stats *t = &HostControl_TransportMetrics;
    bool ok = HostControl_Uint(state, "host_boot_id", HostControl_BootId) &&
              HostControl_Uint64(state, "uptime_ms", HostControl_Uptime) &&
              HostControl_Uint(state, "requests", m->requests) &&
              HostControl_Uint(state, "retries", m->retries) &&
              HostControl_Uint(state, "responses", m->responses) &&
              HostControl_Uint(state, "timeouts", m->timeouts) &&
              HostControl_Uint(state, "protocol_errors", m->protocol_errors) &&
              HostControl_Uint(state, "uart_errors", t->uart_errors) &&
              HostControl_Uint(state, "rx_bytes", t->received_bytes) &&
              HostControl_Uint(state, "tx_bytes", t->transmitted_bytes) &&
              HostControl_Uint(state, "rx_overflows", t->receive_overflows) &&
              HostControl_Uint(state, "tx_overflows", t->transmit_overflows) &&
              HostControl_Uint(state, "events", m->events) &&
              HostControl_Uint(state, "dropped", HostControl_Dropped) &&
              HostControl_Uint(state, "manager_restarts", m->manager_restarts) &&
              HostControl_Uint(state, "online_count", m->online_count) &&
              HostControl_Uint(state, "session_changes", m->session_changes) &&
              HostControl_Uint(state, "ready_losses", m->ready_losses) &&
              HostControl_Uint(state, "node_reboots", m->node_reboots) &&
              HostControl_Uint(state, "latency_count", m->latency_count) &&
              HostControl_Uint64(state, "latency_sum_ms", m->latency_sum_ms) &&
              HostControl_Uint(state, "latency_max_ms", m->latency_max_ms) &&
              HostControl_Uint(state, "latency_last_ms", m->latency_last_ms) &&
              HostControl_Uint(state, "main_stack_free", HostControl_MainFree) &&
              HostControl_Uint(state, "work_stack_free", HostControl_WorkFree) &&
              zcbor_tstr_put_lit(state, "stack_supported") &&
              zcbor_bool_put(state, HostControl_StackSupported);
#if defined(MULTI_MCU_RELIABILITY)
    ok = ok && HostControl_Uint(state, "reliable_duplicates", m->reliable_duplicates) &&
         HostControl_Uint(state, "reliable_gaps", m->reliable_gaps) &&
         HostControl_Uint(state, "event_ack_retries", m->event_ack_retries);
#endif
    k_mutex_unlock(&HostControl_Lock);
    return ok ? MGMT_ERR_EOK : MGMT_ERR_EMSGSIZE;
}
#endif

static const struct mgmt_handler HostControl_Handlers[] = {
#if defined(MULTI_MCU_RELIABILITY)
    [8] = {.mh_read = HostControl_RecoveryRead, .mh_write = HostControl_RecoveryWrite},
#else
    [8] = {.mh_read = HostControl_Unsupported, .mh_write = HostControl_Unsupported},
#endif
    [0] = {.mh_read = HostControl_StatusRead},
    [1] = {.mh_read = HostControl_ServicesRead},
    [2] = {.mh_write = HostControl_CallWrite},
    [3] = {.mh_read = HostControl_EventsRead},
#if defined(MULTI_MCU_PHASE10_UPDATE)
    [4] = {.mh_read = HostUpdateAdapter_StatusRead, .mh_write = HostUpdateAdapter_ResetWrite},
    [5] = {.mh_read = HostUpdateAdapter_StatusRead, .mh_write = HostUpdateAdapter_UpdateWrite},
#else
    [4] = {.mh_write = HostControl_Unsupported},
    [5] = {.mh_write = HostControl_Unsupported},
#endif
#if defined(MULTI_MCU_PHASE11_ROM)
    [6] = {.mh_read = HostUpdateAdapter_RomStatusRead, .mh_write = HostUpdateAdapter_RomWrite},
#else
    [6] = {.mh_write = HostControl_Unsupported},
#endif
#if defined(MULTI_MCU_PHASE12_METRICS)
    [7] = {.mh_read = HostControl_MetricsRead},
#else
    [7] = {.mh_read = HostControl_Unsupported},
#endif
};

static struct mgmt_group HostControl_Group = {.mg_handlers = HostControl_Handlers,
                                              .mg_handlers_count = ARRAY_SIZE(HostControl_Handlers),
                                              .mg_group_id = HOST_CONTROL_GROUP};

static void HostControl_Register(void) { mgmt_register_group(&HostControl_Group); }

MCUMGR_HANDLER_DEFINE(host_control, HostControl_Register);
