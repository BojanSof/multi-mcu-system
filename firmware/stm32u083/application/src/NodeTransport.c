#include "NodeTransport.h"

#include <stddef.h>
#include <string.h>

#include "node_api/NodeApi.h"
#include "node_api/NodeCommands.h"
#include "protocol/ProtocolFrame.h"
#include "protocol/ProtocolTransport.h"
#include "stm32u083_platform/PlatformSideband.h"
#include "stm32u083_platform/PlatformTransport.h"

#define NODE_TRANSPORT_RX_CHUNK_SIZE 32U
#define NODE_TRANSPORT_IDENTITY "stm32u083rc-node"

_Static_assert(sizeof(NODE_TRANSPORT_IDENTITY) - 1U <= NODE_API_IDENTITY_MAX_LENGTH,
               "node identity exceeds the frozen wire limit");

typedef struct {
    NodeTransport_State *state;
    Protocol_Decoder decoder;
    uint8_t rx_chunk[NODE_TRANSPORT_RX_CHUNK_SIZE];
    uint8_t tx_encoded[PROTOCOL_MAX_ENCODED_FRAME];
} NodeTransport_Context;

static NodeTransport_Context NodeTransport_Instance;

static uint16_t NodeTransport_ReadU16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t NodeTransport_ReadU32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) | ((uint32_t)bytes[2] << 16U) |
           ((uint32_t)bytes[3] << 24U);
}

static void NodeTransport_WriteU16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)(value & 0xFFU);
    bytes[1] = (uint8_t)(value >> 8U);
}

static void NodeTransport_WriteU32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value & 0xFFU);
    bytes[1] = (uint8_t)((value >> 8U) & 0xFFU);
    bytes[2] = (uint8_t)((value >> 16U) & 0xFFU);
    bytes[3] = (uint8_t)(value >> 24U);
}

static void NodeTransport_SetPrefix(Protocol_Frame *response, uint16_t message_id,
                                    uint32_t session_generation) {
    NodeTransport_WriteU16(response->payload, message_id);
    NodeTransport_WriteU32(&response->payload[2], session_generation);
    response->payload_length = NODE_API_REQUEST_PREFIX_SIZE;
}

static void NodeTransport_Send(NodeTransport_Context *context, Protocol_Frame *response) {
    size_t encoded_length = 0U;
    if (Protocol_EncodeFrame(response, context->tx_encoded, sizeof(context->tx_encoded),
                             &encoded_length) != PROTOCOL_STATUS_OK) {
        ++context->state->rejected_requests;
        return;
    }
    if (PlatformTransport_Write(context->tx_encoded, encoded_length) != PLATFORM_STATUS_OK) {
        ++context->state->rejected_requests;
    }
}

static void NodeTransport_SendError(NodeTransport_Context *context, uint16_t sequence,
                                    uint16_t message_id, NodeApi_Status status) {
    Protocol_Frame response = {0};
    response.kind = PROTOCOL_FRAME_RESPONSE;
    response.flags = PROTOCOL_FLAG_RESPONSE_ERROR;
    response.sequence = sequence;
    NodeTransport_SetPrefix(&response, message_id, context->state->session_generation);
    response.payload[response.payload_length++] = (uint8_t)status;
    NodeTransport_Send(context, &response);
}

static void NodeTransport_HandleRequest(const Protocol_Frame *request, void *opaque) {
    NodeTransport_Context *context = opaque;
    Protocol_Frame response = {0};
    uint16_t message_id;
    uint32_t requested_session;

    if ((request->kind != PROTOCOL_FRAME_REQUEST) ||
        (request->payload_length < NODE_API_REQUEST_PREFIX_SIZE)) {
        ++context->state->rejected_requests;
        NodeTransport_SendError(context, request->sequence, 0U, NODE_API_STATUS_MALFORMED_REQUEST);
        return;
    }

    message_id = NodeTransport_ReadU16(request->payload);
    requested_session = NodeTransport_ReadU32(&request->payload[2]);
    if ((message_id != NODE_API_MESSAGE_HELLO) &&
        (requested_session != context->state->session_generation)) {
        ++context->state->rejected_requests;
        NodeTransport_SendError(context, request->sequence, message_id,
                                NODE_API_STATUS_STALE_SESSION);
        return;
    }

    response.kind = PROTOCOL_FRAME_RESPONSE;
    response.sequence = request->sequence;
    NodeTransport_SetPrefix(&response, message_id, context->state->session_generation);

    switch (message_id) {
    case NODE_API_MESSAGE_HELLO:
        if (request->payload_length != NODE_API_REQUEST_PREFIX_SIZE) {
            ++context->state->rejected_requests;
            NodeTransport_SendError(context, request->sequence, message_id,
                                    NODE_API_STATUS_MALFORMED_REQUEST);
            return;
        }
        NodeTransport_WriteU16(&response.payload[response.payload_length],
                               NODE_API_MANAGEMENT_VERSION_MAJOR);
        response.payload_length += 2U;
        NodeTransport_WriteU16(&response.payload[response.payload_length],
                               NODE_API_MANAGEMENT_VERSION_MINOR);
        response.payload_length += 2U;
        response.payload[response.payload_length++] = PROTOCOL_WIRE_VERSION_MAJOR;
        break;
    case NODE_API_MESSAGE_GET_IDENTITY: {
        static const char identity[] = NODE_TRANSPORT_IDENTITY;
        const size_t identity_length = sizeof(identity) - 1U;
        if (request->payload_length != NODE_API_REQUEST_PREFIX_SIZE) {
            ++context->state->rejected_requests;
            NodeTransport_SendError(context, request->sequence, message_id,
                                    NODE_API_STATUS_MALFORMED_REQUEST);
            return;
        }
        response.payload[response.payload_length++] = (uint8_t)identity_length;
        memcpy(&response.payload[response.payload_length], identity, identity_length);
        response.payload_length += (uint16_t)identity_length;
        break;
    }
    case NODE_API_MESSAGE_GET_CAPABILITIES:
        if (request->payload_length != NODE_API_REQUEST_PREFIX_SIZE) {
            ++context->state->rejected_requests;
            NodeTransport_SendError(context, request->sequence, message_id,
                                    NODE_API_STATUS_MALFORMED_REQUEST);
            return;
        }
        NodeTransport_WriteU32(
            &response.payload[response.payload_length],
            NODE_API_CAPABILITY_IDENTITY | NODE_API_CAPABILITY_HEARTBEAT |
                (PlatformSideband_IsEnabled() ? NODE_API_CAPABILITY_ASYNC_EVENT_IRQ : 0U));
        response.payload_length += 4U;
        break;
    case NODE_API_MESSAGE_HEARTBEAT: {
        const size_t echo_length = request->payload_length - NODE_API_REQUEST_PREFIX_SIZE;
        if (echo_length > NODE_API_HEARTBEAT_MAX_ECHO) {
            ++context->state->rejected_requests;
            NodeTransport_SendError(context, request->sequence, message_id,
                                    NODE_API_STATUS_MALFORMED_REQUEST);
            return;
        }
        memcpy(&response.payload[response.payload_length],
               &request->payload[NODE_API_REQUEST_PREFIX_SIZE], echo_length);
        response.payload_length += (uint16_t)echo_length;
        break;
    }
    default:
        ++context->state->rejected_requests;
        NodeTransport_SendError(context, request->sequence, message_id,
                                NODE_API_STATUS_UNKNOWN_MESSAGE);
        return;
    }

    ++context->state->accepted_requests;
    NodeTransport_Send(context, &response);
}

static void NodeTransport_HandleDecodeError(Protocol_Status status, void *opaque) {
    NodeTransport_Context *context = opaque;
    (void)status;
    ++context->state->rejected_requests;
}

void NodeTransport_Initialize(NodeTransport_State *state, uint32_t session_generation) {
    if (state == NULL) {
        return;
    }
    memset(&NodeTransport_Instance, 0, sizeof(NodeTransport_Instance));
    state->session_generation = session_generation == 0U ? 1U : session_generation;
    state->accepted_requests = 0U;
    state->rejected_requests = 0U;
    NodeTransport_Instance.state = state;
    Protocol_DecoderInitialize(&NodeTransport_Instance.decoder);
    PlatformSideband_Initialize();
    (void)PlatformTransport_Initialize();
    PlatformSideband_SetReady(PlatformTransport_IsEnabled() != 0);
}

void NodeTransport_Process(NodeTransport_State *state) {
    size_t received;
    if ((state == NULL) || (state != NodeTransport_Instance.state)) {
        return;
    }
    received = PlatformTransport_Read(NodeTransport_Instance.rx_chunk,
                                      sizeof(NodeTransport_Instance.rx_chunk));
    if (received == 0U) {
        return;
    }
    (void)Protocol_ConsumeBytes(&NodeTransport_Instance.decoder, NodeTransport_Instance.rx_chunk,
                                received, NodeTransport_HandleRequest,
                                NodeTransport_HandleDecodeError, &NodeTransport_Instance);
}
