#include "NodeTransport.h"

#include <stddef.h>
#include <string.h>

#include "AppDiagnostics.h"
#include "AppServices.h"
#include "node_api/NodeApi.h"
#include "node_api/NodeCommands.h"
#include "protocol/ProtocolFrame.h"
#include "protocol/ProtocolTransport.h"
#include "stm32u083_platform/PlatformSideband.h"
#include "stm32u083_platform/PlatformTransport.h"
#include "stm32u0xx_hal.h"
#include "virtual_io/VirtualIo.h"

#define NODE_TRANSPORT_RX_CHUNK_SIZE 32U
#define NODE_TRANSPORT_IDENTITY "stm32u083rc-node"

_Static_assert(sizeof(NODE_TRANSPORT_IDENTITY) - 1U <= NODE_API_IDENTITY_MAX_LENGTH,
               "node identity exceeds the frozen wire limit");

typedef struct {
    NodeTransport_State *state;
    NodeApi_Registry *registry;
    Protocol_Decoder decoder;
    uint8_t rx_chunk[NODE_TRANSPORT_RX_CHUNK_SIZE];
    uint8_t tx_encoded[PROTOCOL_MAX_ENCODED_FRAME];
    bool clear_irq_pending;
    bool drop_current_response;
    bool apply_current_injection;
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

static bool NodeTransport_Send(NodeTransport_Context *context, Protocol_Frame *response,
                               bool apply_injection) {
    size_t encoded_length = 0U;
    if (Protocol_EncodeFrame(response, context->tx_encoded, sizeof(context->tx_encoded),
                             &encoded_length) != PROTOCOL_STATUS_OK) {
        ++context->state->rejected_requests;
        return false;
    }
    if (apply_injection) {
        AppDiagnostics_DelayResponse();
        AppDiagnostics_PrepareOutgoing(context->tx_encoded, &encoded_length);
    }
    if (PlatformTransport_Write(context->tx_encoded, encoded_length) != PLATFORM_STATUS_OK) {
        ++context->state->rejected_requests;
        return false;
    }
    return true;
}

static void NodeTransport_SendError(NodeTransport_Context *context, uint16_t sequence,
                                    uint16_t message_id, NodeApi_Status status) {
    Protocol_Frame response = {0};
    response.kind = PROTOCOL_FRAME_RESPONSE;
    response.flags = PROTOCOL_FLAG_RESPONSE_ERROR;
    response.sequence = sequence;
    NodeTransport_SetPrefix(&response, message_id, context->state->session_generation);
    response.payload[response.payload_length++] = (uint8_t)status;
    (void)NodeTransport_Send(context, &response, true);
}

static void NodeTransport_WriteServiceHeader(Protocol_Frame *response, uint16_t service_id,
                                             uint16_t command_or_event_id, uint32_t operation_id) {
    NodeTransport_WriteU16(&response->payload[response->payload_length], service_id);
    response->payload_length += 2U;
    NodeTransport_WriteU16(&response->payload[response->payload_length], command_or_event_id);
    response->payload_length += 2U;
    NodeTransport_WriteU32(&response->payload[response->payload_length], operation_id);
    response->payload_length += 4U;
}

static bool NodeTransport_HandleListServices(NodeTransport_Context *context,
                                             const Protocol_Frame *request,
                                             Protocol_Frame *response) {
    size_t index;
    const size_t service_count = NodeApi_GetServiceCount(context->registry);
    if (request->payload_length != NODE_API_REQUEST_PREFIX_SIZE) {
        NodeTransport_SendError(context, request->sequence, NODE_API_MESSAGE_LIST_SERVICES,
                                NODE_API_STATUS_MALFORMED_REQUEST);
        return false;
    }
    response->payload[response->payload_length++] = (uint8_t)service_count;
    for (index = 0U; index < service_count; ++index) {
        const NodeApi_ServiceDescriptor *descriptor = NodeApi_GetService(context->registry, index);
        if ((descriptor == NULL) ||
            ((size_t)response->payload_length + NODE_API_SERVICE_DESCRIPTOR_SIZE >
             PROTOCOL_MAX_PAYLOAD)) {
            NodeTransport_SendError(context, request->sequence, NODE_API_MESSAGE_LIST_SERVICES,
                                    NODE_API_STATUS_CAPACITY_EXCEEDED);
            return false;
        }
        NodeTransport_WriteU16(&response->payload[response->payload_length],
                               descriptor->service_id);
        response->payload_length += 2U;
        NodeTransport_WriteU16(&response->payload[response->payload_length],
                               descriptor->version_major);
        response->payload_length += 2U;
        NodeTransport_WriteU16(&response->payload[response->payload_length],
                               descriptor->version_minor);
        response->payload_length += 2U;
        NodeTransport_WriteU16(&response->payload[response->payload_length], descriptor->flags);
        response->payload_length += 2U;
    }
    AppServices_MarkOnline();
    return true;
}

static bool NodeTransport_HandleServiceCall(NodeTransport_Context *context,
                                            const Protocol_Frame *request,
                                            Protocol_Frame *response) {
    NodeApi_ServiceRequest service_request = {0};
    NodeApi_ServiceResponse service_response = {0};
    NodeApi_Status status;
    const size_t call_offset = NODE_API_REQUEST_PREFIX_SIZE;
    const size_t data_offset = call_offset + NODE_API_SERVICE_CALL_HEADER_SIZE;
    if ((request->payload_length < data_offset) ||
        ((size_t)request->payload_length - data_offset > NODE_API_SERVICE_DATA_MAX_LENGTH)) {
        NodeTransport_SendError(context, request->sequence, NODE_API_MESSAGE_SERVICE_CALL,
                                NODE_API_STATUS_MALFORMED_REQUEST);
        return false;
    }
    service_request.service_id = NodeTransport_ReadU16(&request->payload[call_offset]);
    service_request.command_id = NodeTransport_ReadU16(&request->payload[call_offset + 2U]);
    service_request.operation_id = NodeTransport_ReadU32(&request->payload[call_offset + 4U]);
    service_request.data = &request->payload[data_offset];
    service_request.data_length = (size_t)request->payload_length - data_offset;
    if (AppServices_IsQuiescentActive() &&
        ((service_request.service_id == VIRTUAL_IO_SENSOR_SERVICE_ID) ||
         (service_request.service_id == VIRTUAL_IO_ACTUATOR_SERVICE_ID))) {
        NodeTransport_SendError(context, request->sequence, NODE_API_MESSAGE_SERVICE_CALL,
                                NODE_API_STATUS_BUSY);
        return false;
    }
    status = NodeApi_DispatchRequest(context->registry, &service_request, &service_response);
    if (status != NODE_API_STATUS_OK) {
        NodeTransport_SendError(context, request->sequence, NODE_API_MESSAGE_SERVICE_CALL, status);
        return false;
    }
    if ((service_response.data_length > NODE_API_SERVICE_DATA_MAX_LENGTH) ||
        ((size_t)response->payload_length + NODE_API_SERVICE_CALL_HEADER_SIZE +
             service_response.data_length >
         PROTOCOL_MAX_PAYLOAD)) {
        NodeTransport_SendError(context, request->sequence, NODE_API_MESSAGE_SERVICE_CALL,
                                NODE_API_STATUS_CAPACITY_EXCEEDED);
        return false;
    }
    NodeTransport_WriteServiceHeader(response, service_request.service_id,
                                     service_request.command_id, service_request.operation_id);
    memcpy(&response->payload[response->payload_length], service_response.data,
           service_response.data_length);
    response->payload_length += (uint16_t)service_response.data_length;
    if ((service_request.service_id == VIRTUAL_IO_ACTUATOR_SERVICE_ID) &&
        (service_request.command_id == VIRTUAL_IO_ACTUATOR_COMMAND_START)) {
        AppDiagnostics_RecordOutputOrdering(service_request.operation_id);
    }
    context->drop_current_response = AppDiagnostics_ShouldDropAcceptance(
        service_request.service_id, service_request.command_id, service_request.operation_id);
    context->apply_current_injection = service_request.service_id != NODE_API_SERVICE_DIAGNOSTICS;
    return true;
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
    context->apply_current_injection = false;
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
                NODE_API_CAPABILITY_LIFECYCLE | NODE_API_CAPABILITY_RESET_REASON |
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
    case NODE_API_MESSAGE_LIST_SERVICES:
        if (!NodeTransport_HandleListServices(context, request, &response)) {
            ++context->state->rejected_requests;
            return;
        }
        break;
    case NODE_API_MESSAGE_SERVICE_CALL:
        if (!NodeTransport_HandleServiceCall(context, request, &response)) {
            ++context->state->rejected_requests;
            return;
        }
        break;
    default:
        ++context->state->rejected_requests;
        NodeTransport_SendError(context, request->sequence, message_id,
                                NODE_API_STATUS_UNKNOWN_MESSAGE);
        return;
    }

    ++context->state->accepted_requests;
    if (context->drop_current_response) {
        context->drop_current_response = false;
        return;
    }
    (void)NodeTransport_Send(context, &response, context->apply_current_injection);
}

static void NodeTransport_HandleDecodeError(Protocol_Status status, void *opaque) {
    NodeTransport_Context *context = opaque;
    (void)status;
    ++context->state->rejected_requests;
}

void NodeTransport_Initialize(NodeTransport_State *state, uint32_t session_generation,
                              NodeApi_Registry *registry) {
    if ((state == NULL) || (registry == NULL)) {
        return;
    }
    memset(&NodeTransport_Instance, 0, sizeof(NodeTransport_Instance));
    state->session_generation = session_generation == 0U ? 1U : session_generation;
    state->accepted_requests = 0U;
    state->rejected_requests = 0U;
    NodeTransport_Instance.state = state;
    NodeTransport_Instance.registry = registry;
    Protocol_DecoderInitialize(&NodeTransport_Instance.decoder);
    PlatformSideband_Initialize();
#if defined(MULTI_MCU_EARLY_READY_BOOT_DEMO)
    PlatformSideband_SetReady(true);
    HAL_Delay(50U);
#endif
    (void)PlatformTransport_Initialize();
    PlatformSideband_SetReady(PlatformTransport_IsEnabled() != 0);
}

void NodeTransport_Process(NodeTransport_State *state) {
    size_t received;
    if ((state == NULL) || (state != NodeTransport_Instance.state)) {
        return;
    }
    if (NodeTransport_Instance.clear_irq_pending) {
        PlatformSideband_SetIrqPending(false);
        NodeTransport_Instance.clear_irq_pending = false;
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

NodeApi_Status NodeTransport_PublishServiceEvent(uint16_t service_id, uint16_t event_id,
                                                 uint32_t operation_id, const uint8_t *data,
                                                 size_t data_length) {
    Protocol_Frame event = {0};
    if ((NodeTransport_Instance.state == NULL) ||
        (data_length > NODE_API_SERVICE_DATA_MAX_LENGTH) ||
        ((data == NULL) && (data_length != 0U))) {
        return NODE_API_STATUS_INVALID_ARGUMENT;
    }
    event.kind = PROTOCOL_FRAME_EVENT;
    event.sequence = 0U;
    NodeTransport_SetPrefix(&event, NODE_API_MESSAGE_SERVICE_EVENT,
                            NodeTransport_Instance.state->session_generation);
    NodeTransport_WriteServiceHeader(&event, service_id, event_id, operation_id);
    if (data_length != 0U) {
        memcpy(&event.payload[event.payload_length], data, data_length);
    }
    event.payload_length += (uint16_t)data_length;
    PlatformSideband_SetIrqPending(true);
    NodeTransport_Instance.clear_irq_pending =
        !AppDiagnostics_IsInjectionEnabled(DIAGNOSTICS_INJECTION_SHORT_IRQ);
    if (!NodeTransport_Send(&NodeTransport_Instance, &event, false)) {
        PlatformSideband_SetIrqPending(false);
        NodeTransport_Instance.clear_irq_pending = false;
        return NODE_API_STATUS_BUSY;
    }
    if (AppDiagnostics_IsInjectionEnabled(DIAGNOSTICS_INJECTION_SHORT_IRQ)) {
        PlatformSideband_SetIrqPending(false);
    }
    return NODE_API_STATUS_OK;
}
