#include "HostNodeManager.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "HostSignals.h"
#include "HostTransport.h"
#include "diagnostics/Diagnostics.h"
#include "node_api/NodeCommands.h"
#include "protocol/ProtocolFrame.h"
#include "protocol/ProtocolTransport.h"
#include "virtual_io/VirtualIo.h"

LOG_MODULE_REGISTER(host_node_manager, LOG_LEVEL_INF);

#define HOST_RESPONSE_TIMEOUT_MS 500
#define HOST_HEARTBEAT_PERIOD_MS 2000
#define HOST_MAX_RETRIES 3U
#define HOST_DIAGNOSTIC_STATUS_SIZE 13U
#define HOST_DIAGNOSTIC_RECORD_SIZE 17U

typedef enum {
    HOST_DIAGNOSTIC_CODE_FRAME_REJECTED = 1U,
    HOST_DIAGNOSTIC_CODE_REQUEST_TIMEOUT = 2U
} HostDiagnostic_Code;

typedef struct {
    Protocol_Decoder decoder;
    HostNodeManager_Status status;
    uint16_t sequence;
    uint16_t pending_message;
    uint16_t pending_service;
    uint16_t pending_command;
    uint32_t pending_operation;
    uint8_t pending_payload[PROTOCOL_MAX_PAYLOAD];
    size_t pending_payload_length;
    Protocol_Frame transmit_frame;
    uint8_t transmit_encoded[PROTOCOL_MAX_ENCODED_FRAME];
    int64_t deadline;
    uint8_t retry_count;
    bool ready;
    Diagnostics_State diagnostics;
} HostNodeManager_Context;

static HostNodeManager_Context HostNodeManager;

static void HostNodeManager_RecordDiagnostic(uint8_t severity, uint8_t domain, uint16_t code,
                                             uint32_t correlation_id, uint32_t detail) {
    Diagnostics_Fault fault = {
        .timestamp_ms = (uint32_t)k_uptime_get(),
        .correlation_id = correlation_id,
        .detail = detail,
        .code = code,
        .source = DIAGNOSTICS_SOURCE_HOST,
        .severity = severity,
        .domain = domain,
    };

    (void)Diagnostics_RecordFault(&HostNodeManager.diagnostics, &fault);
    HostNodeManager.status.diagnostic_records = HostNodeManager.diagnostics.total_recorded;
    HostNodeManager.status.diagnostic_overflows = HostNodeManager.diagnostics.overflow_count;
}

static void HostNodeManager_RejectFrame(uint32_t detail) {
    HostNodeManager.status.rejected_responses++;
    HostNodeManager_RecordDiagnostic(DIAGNOSTICS_SEVERITY_WARNING, DIAGNOSTICS_DOMAIN_PROTOCOL,
                                     HOST_DIAGNOSTIC_CODE_FRAME_REJECTED, HostNodeManager.sequence,
                                     detail);
}

static uint16_t HostNodeManager_ReadU16(const uint8_t *data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
}

static uint32_t HostNodeManager_ReadU32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) | ((uint32_t)data[2] << 16U) |
           ((uint32_t)data[3] << 24U);
}

static int32_t HostNodeManager_ReadI32(const uint8_t *data) {
    return (int32_t)HostNodeManager_ReadU32(data);
}

static void HostNodeManager_LogDiagnosticRecord(const uint8_t *data, size_t data_length) {
    Diagnostics_Fault fault;
    size_t index;

    if (data_length != HOST_DIAGNOSTIC_RECORD_SIZE) {
        HostNodeManager_RejectFrame((uint32_t)data_length);
        return;
    }
    fault.timestamp_ms = HostNodeManager_ReadU32(&data[0]);
    fault.correlation_id = HostNodeManager_ReadU32(&data[4]);
    fault.detail = HostNodeManager_ReadU32(&data[8]);
    fault.code = HostNodeManager_ReadU16(&data[12]);
    fault.source = data[14];
    fault.severity = data[15];
    fault.domain = data[16];
    for (index = 0U; index < HostNodeManager.diagnostics.count; ++index) {
        const size_t record_index =
            (HostNodeManager.diagnostics.head + index) % DIAGNOSTICS_RECORD_CAPACITY;
        const Diagnostics_Fault *existing = &HostNodeManager.diagnostics.records[record_index];
        if ((existing->timestamp_ms == fault.timestamp_ms) &&
            (existing->correlation_id == fault.correlation_id) &&
            (existing->detail == fault.detail) && (existing->code == fault.code) &&
            (existing->source == fault.source) && (existing->severity == fault.severity) &&
            (existing->domain == fault.domain)) {
            return;
        }
    }
    (void)Diagnostics_RecordFault(&HostNodeManager.diagnostics, &fault);
    HostNodeManager.status.diagnostic_records = HostNodeManager.diagnostics.total_recorded;
    HostNodeManager.status.diagnostic_overflows = HostNodeManager.diagnostics.overflow_count;
    LOG_INF("diagnostic record: src %u severity %u domain %u code %u time %u correlation %u "
            "detail 0x%08x",
            fault.source, fault.severity, fault.domain, fault.code, fault.timestamp_ms,
            fault.correlation_id, fault.detail);
}

static bool HostNodeManager_ParseDiagnosticStatus(const uint8_t *data, size_t data_length) {
    if (data_length != HOST_DIAGNOSTIC_STATUS_SIZE) {
        HostNodeManager_RejectFrame((uint32_t)data_length);
        return false;
    }
    HostNodeManager.status.node_injection_mask = HostNodeManager_ReadU32(&data[0]);
    HostNodeManager.status.node_diagnostic_queued = data[12];
    LOG_INF("node diagnostics: injections 0x%08x, records %u, overflows %u, queued %u",
            HostNodeManager.status.node_injection_mask, HostNodeManager_ReadU32(&data[4]),
            HostNodeManager_ReadU32(&data[8]), data[12]);
    return true;
}

static void HostNodeManager_WriteU16(uint8_t *data, uint16_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
}

static void HostNodeManager_WriteU32(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

static int HostNodeManager_TransmitPending(bool retry) {
    size_t encoded_length = 0U;
    Protocol_Status status;
    Protocol_Frame *const frame = &HostNodeManager.transmit_frame;

    memset(frame, 0, sizeof(*frame));
    frame->kind = PROTOCOL_FRAME_REQUEST;
    frame->sequence = retry ? HostNodeManager.sequence : ++HostNodeManager.sequence;
    if (frame->sequence == 0U) {
        frame->sequence = ++HostNodeManager.sequence;
    }
    frame->payload_length = HostNodeManager.pending_payload_length;
    memcpy(frame->payload, HostNodeManager.pending_payload, frame->payload_length);

    status = Protocol_EncodeFrame(frame, HostNodeManager.transmit_encoded,
                                  sizeof(HostNodeManager.transmit_encoded), &encoded_length);
    if (status != PROTOCOL_STATUS_OK) {
        return -EINVAL;
    }
    if (HostTransport_Write(HostNodeManager.transmit_encoded, encoded_length) != 0) {
        return -EIO;
    }

    HostNodeManager.deadline = k_uptime_get() + HOST_RESPONSE_TIMEOUT_MS;
    if (!retry) {
        HostNodeManager.retry_count = 0U;
    }
    return 0;
}

static int HostNodeManager_SendRequest(uint16_t message_id, const uint8_t *data,
                                       size_t data_length) {
    int result;

    if (HostNodeManager.pending_message != 0U ||
        data_length > PROTOCOL_MAX_PAYLOAD - NODE_API_REQUEST_PREFIX_SIZE ||
        (data_length != 0U && data == NULL)) {
        return -EBUSY;
    }

    HostNodeManager.pending_message = message_id;
    HostNodeManager.pending_payload_length = NODE_API_REQUEST_PREFIX_SIZE + data_length;
    HostNodeManager_WriteU16(HostNodeManager.pending_payload, message_id);
    HostNodeManager_WriteU32(&HostNodeManager.pending_payload[2],
                             HostNodeManager.status.session_generation);
    if (data_length != 0U) {
        memcpy(&HostNodeManager.pending_payload[NODE_API_REQUEST_PREFIX_SIZE], data, data_length);
    }
    result = HostNodeManager_TransmitPending(false);
    if (result != 0) {
        HostNodeManager.pending_message = 0U;
        HostNodeManager.pending_payload_length = 0U;
    }
    return result;
}

static void HostNodeManager_CompleteRequest(void) {
    HostNodeManager.pending_message = 0U;
    HostNodeManager.pending_payload_length = 0U;
    HostNodeManager.retry_count = 0U;
    HostNodeManager.deadline = k_uptime_get() + HOST_HEARTBEAT_PERIOD_MS;
}

static void HostNodeManager_Advance(uint16_t message_id, const Protocol_Frame *frame) {
    uint32_t session = HostNodeManager_ReadU32(&frame->payload[2]);

    if (message_id == NODE_API_MESSAGE_HELLO) {
        if (frame->payload_length != NODE_API_HELLO_RESPONSE_SIZE || session == 0U ||
            HostNodeManager_ReadU16(&frame->payload[6]) != NODE_API_MANAGEMENT_VERSION_MAJOR ||
            frame->payload[10] != PROTOCOL_WIRE_VERSION_MAJOR) {
            HostNodeManager.status.rejected_responses++;
            return;
        }
        HostNodeManager.status.session_generation = session;
        HostNodeManager.status.state = HOST_LINK_IDENTITY;
        HostNodeManager_CompleteRequest();
        (void)HostNodeManager_SendRequest(NODE_API_MESSAGE_GET_IDENTITY, NULL, 0U);
    } else if (message_id == NODE_API_MESSAGE_GET_IDENTITY) {
        size_t identity_length;

        if (frame->payload_length < NODE_API_REQUEST_PREFIX_SIZE + 1U ||
            session != HostNodeManager.status.session_generation) {
            HostNodeManager.status.rejected_responses++;
            return;
        }
        identity_length = frame->payload[6];
        if (identity_length > NODE_API_IDENTITY_MAX_LENGTH ||
            frame->payload_length != NODE_API_REQUEST_PREFIX_SIZE + 1U + identity_length) {
            HostNodeManager.status.rejected_responses++;
            return;
        }
        memcpy(HostNodeManager.status.identity, &frame->payload[7], identity_length);
        HostNodeManager.status.identity[identity_length] = '\0';
        HostNodeManager.status.state = HOST_LINK_CAPABILITIES;
        HostNodeManager_CompleteRequest();
        (void)HostNodeManager_SendRequest(NODE_API_MESSAGE_GET_CAPABILITIES, NULL, 0U);
    } else if (message_id == NODE_API_MESSAGE_GET_CAPABILITIES) {
        if (frame->payload_length != NODE_API_CAPABILITIES_RESPONSE_SIZE ||
            session != HostNodeManager.status.session_generation) {
            HostNodeManager.status.rejected_responses++;
            return;
        }
        HostNodeManager.status.capabilities = HostNodeManager_ReadU32(&frame->payload[6]);
        HostNodeManager.status.state = HOST_LINK_SERVICES;
        HostNodeManager_CompleteRequest();
        (void)HostNodeManager_SendRequest(NODE_API_MESSAGE_LIST_SERVICES, NULL, 0U);
    } else if (message_id == NODE_API_MESSAGE_LIST_SERVICES) {
        size_t count;
        size_t index;

        if (frame->payload_length < NODE_API_REQUEST_PREFIX_SIZE + 1U ||
            session != HostNodeManager.status.session_generation) {
            HostNodeManager.status.rejected_responses++;
            return;
        }
        count = frame->payload[NODE_API_REQUEST_PREFIX_SIZE];
        if (count > NODE_API_MAX_SERVICES ||
            frame->payload_length !=
                NODE_API_REQUEST_PREFIX_SIZE + 1U + (count * NODE_API_SERVICE_DESCRIPTOR_SIZE)) {
            HostNodeManager.status.rejected_responses++;
            return;
        }
        HostNodeManager.status.service_count = count;
        for (index = 0U; index < count; ++index) {
            const uint8_t *descriptor = &frame->payload[NODE_API_REQUEST_PREFIX_SIZE + 1U +
                                                        (index * NODE_API_SERVICE_DESCRIPTOR_SIZE)];
            NodeApi_ServiceDescriptor *service = &HostNodeManager.status.services[index];

            service->service_id = HostNodeManager_ReadU16(&descriptor[0]);
            service->version_major = HostNodeManager_ReadU16(&descriptor[2]);
            service->version_minor = HostNodeManager_ReadU16(&descriptor[4]);
            service->flags = HostNodeManager_ReadU16(&descriptor[6]);
            LOG_INF("service 0x%04x v%u.%u flags 0x%04x", service->service_id,
                    service->version_major, service->version_minor, service->flags);
        }
        HostNodeManager.status.state = HOST_LINK_ONLINE;
        HostNodeManager_CompleteRequest();
        LOG_INF("node online: %s, session %u, capabilities 0x%08x", HostNodeManager.status.identity,
                HostNodeManager.status.session_generation, HostNodeManager.status.capabilities);
    } else if (message_id == NODE_API_MESSAGE_HEARTBEAT) {
        if (frame->payload_length != NODE_API_REQUEST_PREFIX_SIZE ||
            session != HostNodeManager.status.session_generation) {
            HostNodeManager.status.rejected_responses++;
            HostNodeManager.status.state = HOST_LINK_DISCOVERING;
            HostNodeManager.status.session_generation = 0U;
            HostNodeManager.pending_message = 0U;
            (void)HostNodeManager_SendRequest(NODE_API_MESSAGE_HELLO, NULL, 0U);
            return;
        }
        HostNodeManager_CompleteRequest();
    } else if (message_id == NODE_API_MESSAGE_SERVICE_CALL) {
        const uint8_t *response_data =
            &frame->payload[NODE_API_REQUEST_PREFIX_SIZE + NODE_API_SERVICE_CALL_HEADER_SIZE];
        const size_t response_data_length = frame->payload_length - NODE_API_REQUEST_PREFIX_SIZE -
                                            NODE_API_SERVICE_CALL_HEADER_SIZE;

        if (frame->payload_length <
                NODE_API_REQUEST_PREFIX_SIZE + NODE_API_SERVICE_CALL_HEADER_SIZE ||
            session != HostNodeManager.status.session_generation ||
            HostNodeManager_ReadU16(&frame->payload[6]) != HostNodeManager.pending_service ||
            HostNodeManager_ReadU16(&frame->payload[8]) != HostNodeManager.pending_command ||
            HostNodeManager_ReadU32(&frame->payload[10]) != HostNodeManager.pending_operation) {
            HostNodeManager.status.rejected_responses++;
            return;
        }
        LOG_INF("service 0x%04x command 0x%04x operation %u accepted",
                HostNodeManager.pending_service, HostNodeManager.pending_command,
                HostNodeManager.pending_operation);
        if (HostNodeManager.pending_service == NODE_API_SERVICE_DIAGNOSTICS &&
            (HostNodeManager.pending_command == NODE_API_DIAGNOSTICS_COMMAND_GET_STATUS ||
             HostNodeManager.pending_command == NODE_API_DIAGNOSTICS_COMMAND_ENABLE_INJECTION ||
             HostNodeManager.pending_command == NODE_API_DIAGNOSTICS_COMMAND_DISABLE_ALL)) {
            (void)HostNodeManager_ParseDiagnosticStatus(response_data, response_data_length);
        } else if (HostNodeManager.pending_service == NODE_API_SERVICE_DIAGNOSTICS &&
                   HostNodeManager.pending_command == NODE_API_DIAGNOSTICS_COMMAND_GET_RECORD) {
            if (response_data_length == 1U && response_data[0] == 0U) {
                LOG_INF("node diagnostic record queue empty");
                HostNodeManager.status.node_diagnostic_queued = 0U;
            } else if (response_data_length == 1U + HOST_DIAGNOSTIC_RECORD_SIZE &&
                       response_data[0] == 1U) {
                HostNodeManager_LogDiagnosticRecord(&response_data[1], HOST_DIAGNOSTIC_RECORD_SIZE);
                /* Keep draining until the node explicitly reports an empty queue. */
                HostNodeManager.status.node_diagnostic_queued = 1U;
            } else {
                HostNodeManager_RejectFrame((uint32_t)response_data_length);
            }
        } else if (HostNodeManager.pending_service == VIRTUAL_IO_SENSOR_SERVICE_ID &&
                   HostNodeManager.pending_command == VIRTUAL_IO_SENSOR_COMMAND_SAMPLE &&
                   response_data_length == VIRTUAL_IO_SENSOR_SAMPLE_RESPONSE_SIZE) {
            LOG_INF("virtual sensor sample: value %d milli-units, index %u",
                    HostNodeManager_ReadI32(response_data),
                    HostNodeManager_ReadU32(&response_data[4]));
        } else if (HostNodeManager.pending_service == VIRTUAL_IO_ACTUATOR_SERVICE_ID &&
                   HostNodeManager.pending_command == VIRTUAL_IO_ACTUATOR_COMMAND_START &&
                   response_data_length == VIRTUAL_IO_ACTUATOR_START_RESPONSE_SIZE) {
            LOG_INF("virtual actuator execution %u", HostNodeManager_ReadU32(response_data));
        }
        HostNodeManager_CompleteRequest();
    }
}

static void HostNodeManager_EventReceived(const Protocol_Frame *frame) {
    uint16_t message_id;
    uint16_t service_id;
    uint16_t event_id;
    uint32_t operation_id;

    if (frame->sequence != 0U ||
        frame->payload_length < NODE_API_REQUEST_PREFIX_SIZE + NODE_API_SERVICE_EVENT_HEADER_SIZE) {
        HostNodeManager.status.rejected_responses++;
        return;
    }
    message_id = HostNodeManager_ReadU16(frame->payload);
    if (message_id != NODE_API_MESSAGE_SERVICE_EVENT) {
        HostNodeManager.status.rejected_responses++;
        return;
    }
    if (HostNodeManager_ReadU32(&frame->payload[2]) != HostNodeManager.status.session_generation) {
        HostNodeManager.status.rejected_responses++;
        return;
    }
    service_id = HostNodeManager_ReadU16(&frame->payload[6]);
    event_id = HostNodeManager_ReadU16(&frame->payload[8]);
    operation_id = HostNodeManager_ReadU32(&frame->payload[10]);
    HostNodeManager.status.received_events++;
    const uint8_t *event_data =
        &frame->payload[NODE_API_REQUEST_PREFIX_SIZE + NODE_API_SERVICE_EVENT_HEADER_SIZE];
    const size_t event_data_length =
        frame->payload_length - NODE_API_REQUEST_PREFIX_SIZE - NODE_API_SERVICE_EVENT_HEADER_SIZE;

    if (event_data_length > NODE_API_SERVICE_DATA_MAX_LENGTH) {
        HostNodeManager.status.rejected_responses++;
        return;
    }

    if (service_id == NODE_API_SERVICE_DIAGNOSTICS &&
        event_id == NODE_API_DIAGNOSTICS_EVENT_RECORD) {
        HostNodeManager_LogDiagnosticRecord(event_data, event_data_length);
        /* An event is also a notification that its durable queue record is available. */
        HostNodeManager.status.node_diagnostic_queued = 1U;
    } else if (service_id == NODE_API_SERVICE_GPIO &&
               event_id == NODE_API_GPIO_EVENT_BUTTON_CHANGED &&
               event_data_length == NODE_API_GPIO_BUTTON_EVENT_DATA_SIZE) {
        LOG_INF("node button %s (event %u)", event_data[0] != 0U ? "pressed" : "released",
                operation_id);
    } else if (service_id == VIRTUAL_IO_ACTUATOR_SERVICE_ID &&
               event_id == VIRTUAL_IO_ACTUATOR_EVENT_PROGRESS &&
               event_data_length == VIRTUAL_IO_ACTUATOR_PROGRESS_EVENT_SIZE) {
        LOG_INF("virtual actuator operation %u progress %u%%, execution %u", operation_id,
                event_data[0], HostNodeManager_ReadU32(&event_data[1]));
    } else if (service_id == VIRTUAL_IO_ACTUATOR_SERVICE_ID &&
               event_id == VIRTUAL_IO_ACTUATOR_EVENT_COMPLETE &&
               event_data_length == VIRTUAL_IO_ACTUATOR_COMPLETE_EVENT_SIZE) {
        LOG_INF("virtual actuator operation %u complete, execution %u", operation_id,
                HostNodeManager_ReadU32(event_data));
    } else {
        LOG_INF("service event: service 0x%04x event 0x%04x operation %u data %u bytes", service_id,
                event_id, operation_id, (unsigned int)event_data_length);
    }
}

static void HostNodeManager_FrameReceived(const Protocol_Frame *frame, void *context) {
    uint16_t message_id;

    ARG_UNUSED(context);
    if (frame->kind == PROTOCOL_FRAME_EVENT) {
        HostNodeManager_EventReceived(frame);
        return;
    }
    if (frame->kind != PROTOCOL_FRAME_RESPONSE || frame->sequence != HostNodeManager.sequence ||
        frame->payload_length < NODE_API_REQUEST_PREFIX_SIZE) {
        HostNodeManager.status.rejected_responses++;
        return;
    }
    message_id = HostNodeManager_ReadU16(frame->payload);
    if (message_id != HostNodeManager.pending_message ||
        (message_id != NODE_API_MESSAGE_HELLO && HostNodeManager_ReadU32(&frame->payload[2]) !=
                                                     HostNodeManager.status.session_generation)) {
        HostNodeManager.status.rejected_responses++;
        return;
    }
    if ((frame->flags & PROTOCOL_FLAG_RESPONSE_ERROR) != 0U) {
        if (frame->payload_length == NODE_API_ERROR_RESPONSE_SIZE) {
            LOG_WRN("request 0x%04x rejected with status %u", message_id, frame->payload[6]);
            if (message_id == NODE_API_MESSAGE_SERVICE_CALL) {
                HostNodeManager_CompleteRequest();
                return;
            }
        }
        HostNodeManager.status.rejected_responses++;
        return;
    }
    HostNodeManager_Advance(message_id, frame);
}

static void HostNodeManager_FrameError(Protocol_Status status, void *context) {
    ARG_UNUSED(context);
    HostNodeManager_RejectFrame((uint32_t)status);
}

void HostNodeManager_Initialize(void) {
    memset(&HostNodeManager, 0, sizeof(HostNodeManager));
    Diagnostics_Initialize(&HostNodeManager.diagnostics);
    Protocol_DecoderInitialize(&HostNodeManager.decoder);
    if (!HostTransport_IsAvailable()) {
        HostNodeManager.status.state = HOST_LINK_DISABLED;
        return;
    }
    HostNodeManager.status.state = HOST_LINK_DISCOVERING;
    HostNodeManager.ready = HostSignals_NodeReady();
    if (HostNodeManager.ready) {
        (void)HostNodeManager_SendRequest(NODE_API_MESSAGE_HELLO, NULL, 0U);
    }
}

void HostNodeManager_Process(void) {
    uint8_t received[64];
    size_t count;
    int64_t now;
    const bool ready = HostSignals_NodeReady();

    do {
        count = HostTransport_Read(received, sizeof(received));
        if (count != 0U) {
            (void)Protocol_ConsumeBytes(&HostNodeManager.decoder, received, count,
                                        HostNodeManager_FrameReceived, HostNodeManager_FrameError,
                                        NULL);
        }
    } while (count != 0U);

    if (HostNodeManager.status.state == HOST_LINK_DISABLED) {
        return;
    }
    if (!ready) {
        HostNodeManager.ready = false;
        HostNodeManager.status.state = HOST_LINK_DISCOVERING;
        HostNodeManager.status.session_generation = 0U;
        HostNodeManager.status.service_count = 0U;
        HostNodeManager.pending_message = 0U;
        HostNodeManager.deadline = 0;
        HostNodeManager.retry_count = 0U;
        return;
    }
    if (!HostNodeManager.ready) {
        HostNodeManager.ready = true;
        (void)HostNodeManager_SendRequest(NODE_API_MESSAGE_HELLO, NULL, 0U);
        return;
    }
    now = k_uptime_get();
    if (now < HostNodeManager.deadline) {
        return;
    }
    if (HostNodeManager.status.state == HOST_LINK_ONLINE && HostNodeManager.pending_message == 0U) {
        (void)HostNodeManager_SendRequest(NODE_API_MESSAGE_HEARTBEAT, NULL, 0U);
        return;
    }
    if (HostNodeManager.retry_count < HOST_MAX_RETRIES && HostNodeManager.pending_message != 0U) {
        HostNodeManager.retry_count++;
        HostNodeManager.status.timeouts++;
        HostNodeManager_RecordDiagnostic(DIAGNOSTICS_SEVERITY_WARNING, DIAGNOSTICS_DOMAIN_TRANSPORT,
                                         HOST_DIAGNOSTIC_CODE_REQUEST_TIMEOUT,
                                         HostNodeManager.pending_operation,
                                         HostNodeManager.pending_message);
        (void)HostNodeManager_TransmitPending(true);
        return;
    }

    HostNodeManager.status.state = HOST_LINK_DEGRADED;
    HostNodeManager.status.timeouts++;
    HostNodeManager_RecordDiagnostic(DIAGNOSTICS_SEVERITY_ERROR, DIAGNOSTICS_DOMAIN_TRANSPORT,
                                     HOST_DIAGNOSTIC_CODE_REQUEST_TIMEOUT,
                                     HostNodeManager.pending_operation,
                                     HostNodeManager.pending_message);
    HostNodeManager.status.session_generation = 0U;
    HostNodeManager.status.state = HOST_LINK_DISCOVERING;
    HostNodeManager.pending_message = 0U;
    (void)HostNodeManager_SendRequest(NODE_API_MESSAGE_HELLO, NULL, 0U);
}

HostNodeManager_Status HostNodeManager_GetStatus(void) { return HostNodeManager.status; }

const char *HostNodeManager_StateName(HostLink_State state) {
    static const char *const names[] = {"disabled", "discovering", "identity", "capabilities",
                                        "services", "online",      "degraded"};
    return state <= HOST_LINK_DEGRADED ? names[state] : "unknown";
}

bool HostNodeManager_HasService(uint16_t service_id) {
    size_t index;

    for (index = 0U; index < HostNodeManager.status.service_count; ++index) {
        if (HostNodeManager.status.services[index].service_id == service_id) {
            return true;
        }
    }
    return false;
}

int HostNodeManager_CallService(uint16_t service_id, uint16_t command_id, const uint8_t *data,
                                size_t data_length, uint32_t *operation_id) {
    uint8_t call[NODE_API_SERVICE_CALL_HEADER_SIZE + NODE_API_SERVICE_DATA_MAX_LENGTH];
    uint32_t next_operation;

    if (HostNodeManager.status.state != HOST_LINK_ONLINE ||
        !HostNodeManager_HasService(service_id)) {
        return -ENOTSUP;
    }
    if (HostNodeManager.pending_message != 0U) {
        return -EBUSY;
    }
    if (data_length > NODE_API_SERVICE_DATA_MAX_LENGTH || (data_length != 0U && data == NULL)) {
        return -EINVAL;
    }
    next_operation = ++HostNodeManager.status.next_operation_id;
    if (next_operation == 0U) {
        next_operation = ++HostNodeManager.status.next_operation_id;
    }
    HostNodeManager_WriteU16(&call[0], service_id);
    HostNodeManager_WriteU16(&call[2], command_id);
    HostNodeManager_WriteU32(&call[4], next_operation);
    if (data_length != 0U) {
        memcpy(&call[NODE_API_SERVICE_CALL_HEADER_SIZE], data, data_length);
    }
    HostNodeManager.pending_service = service_id;
    HostNodeManager.pending_command = command_id;
    HostNodeManager.pending_operation = next_operation;
    if (HostNodeManager_SendRequest(NODE_API_MESSAGE_SERVICE_CALL, call,
                                    NODE_API_SERVICE_CALL_HEADER_SIZE + data_length) != 0) {
        return -EBUSY;
    }
    if (operation_id != NULL) {
        *operation_id = next_operation;
    }
    return 0;
}

int HostNodeManager_DiagnosticsGetStatus(void) {
    return HostNodeManager_CallService(NODE_API_SERVICE_DIAGNOSTICS,
                                       NODE_API_DIAGNOSTICS_COMMAND_GET_STATUS, NULL, 0U, NULL);
}

int HostNodeManager_DiagnosticsEnable(uint8_t injection_id) {
    if (injection_id < (uint8_t)DIAGNOSTICS_INJECTION_EARLY_READY ||
        injection_id > (uint8_t)DIAGNOSTICS_INJECTION_MAX) {
        return -EINVAL;
    }
    return HostNodeManager_CallService(NODE_API_SERVICE_DIAGNOSTICS,
                                       NODE_API_DIAGNOSTICS_COMMAND_ENABLE_INJECTION, &injection_id,
                                       sizeof(injection_id), NULL);
}

int HostNodeManager_DiagnosticsDisableAll(void) {
    return HostNodeManager_CallService(NODE_API_SERVICE_DIAGNOSTICS,
                                       NODE_API_DIAGNOSTICS_COMMAND_DISABLE_ALL, NULL, 0U, NULL);
}

int HostNodeManager_DiagnosticsGetRecord(void) {
    return HostNodeManager_CallService(NODE_API_SERVICE_DIAGNOSTICS,
                                       NODE_API_DIAGNOSTICS_COMMAND_GET_RECORD, NULL, 0U, NULL);
}
