#include "HostNodeManager.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "HostSignals.h"
#include "HostTransport.h"
#include "node_api/NodeCommands.h"
#include "protocol/ProtocolFrame.h"
#include "protocol/ProtocolTransport.h"

LOG_MODULE_REGISTER(host_node_manager, LOG_LEVEL_INF);

#define HOST_RESPONSE_TIMEOUT_MS 500
#define HOST_HEARTBEAT_PERIOD_MS 2000
#define HOST_MAX_RETRIES 3U

typedef struct {
    Protocol_Decoder decoder;
    HostNodeManager_Status status;
    uint16_t sequence;
    uint16_t pending_message;
    int64_t deadline;
    uint8_t retry_count;
    bool ready;
} HostNodeManager_Context;

static HostNodeManager_Context HostNodeManager;

static uint16_t HostNodeManager_ReadU16(const uint8_t *data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
}

static uint32_t HostNodeManager_ReadU32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) | ((uint32_t)data[2] << 16U) |
           ((uint32_t)data[3] << 24U);
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

static int HostNodeManager_Send(uint16_t message_id, bool retry) {
    Protocol_Frame frame = {0};
    uint8_t encoded[PROTOCOL_MAX_ENCODED_FRAME];
    size_t encoded_length = 0U;
    Protocol_Status status;

    frame.kind = PROTOCOL_FRAME_REQUEST;
    frame.sequence = retry ? HostNodeManager.sequence : ++HostNodeManager.sequence;
    if (frame.sequence == 0U) {
        frame.sequence = ++HostNodeManager.sequence;
    }
    frame.payload_length = NODE_API_REQUEST_PREFIX_SIZE;
    HostNodeManager_WriteU16(frame.payload, message_id);
    HostNodeManager_WriteU32(&frame.payload[2], HostNodeManager.status.session_generation);

    status = Protocol_EncodeFrame(&frame, encoded, sizeof(encoded), &encoded_length);
    if (status != PROTOCOL_STATUS_OK) {
        return -EINVAL;
    }
    if (HostTransport_Write(encoded, encoded_length) != 0) {
        return -EIO;
    }

    HostNodeManager.pending_message = message_id;
    HostNodeManager.deadline = k_uptime_get() + HOST_RESPONSE_TIMEOUT_MS;
    if (!retry) {
        HostNodeManager.retry_count = 0U;
    }
    return 0;
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
        (void)HostNodeManager_Send(NODE_API_MESSAGE_GET_IDENTITY, false);
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
        (void)HostNodeManager_Send(NODE_API_MESSAGE_GET_CAPABILITIES, false);
    } else if (message_id == NODE_API_MESSAGE_GET_CAPABILITIES) {
        if (frame->payload_length != NODE_API_CAPABILITIES_RESPONSE_SIZE ||
            session != HostNodeManager.status.session_generation) {
            HostNodeManager.status.rejected_responses++;
            return;
        }
        HostNodeManager.status.capabilities = HostNodeManager_ReadU32(&frame->payload[6]);
        HostNodeManager.status.state = HOST_LINK_ONLINE;
        HostNodeManager.pending_message = 0U;
        HostNodeManager.deadline = k_uptime_get() + HOST_HEARTBEAT_PERIOD_MS;
        LOG_INF("node online: %s, session %u, capabilities 0x%08x", HostNodeManager.status.identity,
                HostNodeManager.status.session_generation, HostNodeManager.status.capabilities);
    } else if (message_id == NODE_API_MESSAGE_HEARTBEAT) {
        if (frame->payload_length != NODE_API_REQUEST_PREFIX_SIZE ||
            session != HostNodeManager.status.session_generation) {
            HostNodeManager.status.rejected_responses++;
            HostNodeManager.status.state = HOST_LINK_DISCOVERING;
            HostNodeManager.status.session_generation = 0U;
            (void)HostNodeManager_Send(NODE_API_MESSAGE_HELLO, false);
            return;
        }
        HostNodeManager.pending_message = 0U;
        HostNodeManager.deadline = k_uptime_get() + HOST_HEARTBEAT_PERIOD_MS;
    }
}

static void HostNodeManager_FrameReceived(const Protocol_Frame *frame, void *context) {
    uint16_t message_id;

    ARG_UNUSED(context);
    if (frame->kind != PROTOCOL_FRAME_RESPONSE || frame->sequence != HostNodeManager.sequence ||
        frame->payload_length < NODE_API_REQUEST_PREFIX_SIZE ||
        (frame->flags & PROTOCOL_FLAG_RESPONSE_ERROR) != 0U) {
        HostNodeManager.status.rejected_responses++;
        return;
    }
    message_id = HostNodeManager_ReadU16(frame->payload);
    if (message_id != HostNodeManager.pending_message) {
        HostNodeManager.status.rejected_responses++;
        return;
    }
    HostNodeManager_Advance(message_id, frame);
}

static void HostNodeManager_FrameError(Protocol_Status status, void *context) {
    ARG_UNUSED(status);
    ARG_UNUSED(context);
    HostNodeManager.status.rejected_responses++;
}

void HostNodeManager_Initialize(void) {
    memset(&HostNodeManager, 0, sizeof(HostNodeManager));
    Protocol_DecoderInitialize(&HostNodeManager.decoder);
    if (!HostTransport_IsAvailable()) {
        HostNodeManager.status.state = HOST_LINK_DISABLED;
        return;
    }
    HostNodeManager.status.state = HOST_LINK_DISCOVERING;
    HostNodeManager.ready = HostSignals_NodeReady();
    if (HostNodeManager.ready) {
        (void)HostNodeManager_Send(NODE_API_MESSAGE_HELLO, false);
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
        HostNodeManager.pending_message = 0U;
        HostNodeManager.deadline = 0;
        HostNodeManager.retry_count = 0U;
        return;
    }
    if (!HostNodeManager.ready) {
        HostNodeManager.ready = true;
        (void)HostNodeManager_Send(NODE_API_MESSAGE_HELLO, false);
        return;
    }
    now = k_uptime_get();
    if (now < HostNodeManager.deadline) {
        return;
    }
    if (HostNodeManager.status.state == HOST_LINK_ONLINE && HostNodeManager.pending_message == 0U) {
        (void)HostNodeManager_Send(NODE_API_MESSAGE_HEARTBEAT, false);
        return;
    }
    if (HostNodeManager.retry_count < HOST_MAX_RETRIES && HostNodeManager.pending_message != 0U) {
        HostNodeManager.retry_count++;
        HostNodeManager.status.timeouts++;
        (void)HostNodeManager_Send(HostNodeManager.pending_message, true);
        return;
    }

    HostNodeManager.status.state = HOST_LINK_DEGRADED;
    HostNodeManager.status.timeouts++;
    HostNodeManager.status.session_generation = 0U;
    HostNodeManager.status.state = HOST_LINK_DISCOVERING;
    (void)HostNodeManager_Send(NODE_API_MESSAGE_HELLO, false);
}

HostNodeManager_Status HostNodeManager_GetStatus(void) { return HostNodeManager.status; }

const char *HostNodeManager_StateName(HostLink_State state) {
    static const char *const names[] = {"disabled",     "discovering", "identity",
                                        "capabilities", "online",      "degraded"};
    return state <= HOST_LINK_DEGRADED ? names[state] : "unknown";
}
