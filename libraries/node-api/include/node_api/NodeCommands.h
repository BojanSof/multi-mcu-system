#ifndef NODE_API_NODE_COMMANDS_H
#define NODE_API_NODE_COMMANDS_H

#include <stdint.h>

#define NODE_API_MANAGEMENT_VERSION_MAJOR 1U
#define NODE_API_MANAGEMENT_VERSION_MINOR 0U
#define NODE_API_IDENTITY_MAX_LENGTH 32U
#define NODE_API_REQUEST_PREFIX_SIZE 6U
#define NODE_API_HELLO_RESPONSE_SIZE 11U
#define NODE_API_CAPABILITIES_RESPONSE_SIZE 10U
#define NODE_API_ERROR_RESPONSE_SIZE 7U
#define NODE_API_HEARTBEAT_MAX_ECHO 16U
#define NODE_API_MAX_SERVICES 8U
#define NODE_API_SERVICE_DATA_MAX_LENGTH 64U
#define NODE_API_SERVICE_DESCRIPTOR_SIZE 8U
#define NODE_API_SERVICE_CALL_HEADER_SIZE 8U
#define NODE_API_SERVICE_EVENT_HEADER_SIZE 8U

typedef enum {
    NODE_API_MESSAGE_HELLO = 0x0001,
    NODE_API_MESSAGE_GET_IDENTITY = 0x0002,
    NODE_API_MESSAGE_GET_CAPABILITIES = 0x0003,
    NODE_API_MESSAGE_HEARTBEAT = 0x0004,
    NODE_API_MESSAGE_LIST_SERVICES = 0x0005,
    NODE_API_MESSAGE_ENABLE_RELIABLE_EVENTS = 0x0006,
    NODE_API_MESSAGE_EVENT_ACK = 0x0007,
    NODE_API_MESSAGE_SERVICE_CALL = 0x0100,
    NODE_API_MESSAGE_SERVICE_EVENT = 0x8000,
    NODE_API_MESSAGE_RELIABLE_EVENT = 0x8001
} NodeApi_MessageId;

typedef enum {
    NODE_API_CAPABILITY_IDENTITY = (1UL << 0),
    NODE_API_CAPABILITY_HEARTBEAT = (1UL << 1),
    NODE_API_CAPABILITY_ASYNC_EVENT_IRQ = (1UL << 2),
    NODE_API_CAPABILITY_LIFECYCLE = (1UL << 3),
    NODE_API_CAPABILITY_RESET_REASON = (1UL << 4),
    NODE_API_CAPABILITY_RELIABLE_EVENTS = (1UL << 5)
} NodeApi_Capability;

typedef enum {
    NODE_API_STATUS_OK = 0,
    NODE_API_STATUS_MALFORMED_REQUEST = 1,
    NODE_API_STATUS_UNKNOWN_MESSAGE = 2,
    NODE_API_STATUS_STALE_SESSION = 3,
    NODE_API_STATUS_UNSUPPORTED_VERSION = 4,
    NODE_API_STATUS_UNSUPPORTED_SERVICE = 5,
    NODE_API_STATUS_UNSUPPORTED_COMMAND = 6,
    NODE_API_STATUS_BUSY = 7,
    NODE_API_STATUS_INVALID_ARGUMENT = 8,
    NODE_API_STATUS_CAPACITY_EXCEEDED = 9
} NodeApi_Status;

/*
 * Phase 2 wire payloads use explicit little-endian fields and are encoded by
 * transport adapters without casting these declarations onto wire storage.
 *
 * Request prefix:       message_id:u16, session_generation:u32
 * Response prefix:      message_id:u16, node_session_generation:u32
 * HELLO response tail:  management_major:u16, management_minor:u16,
 *                       wire_major:u8
 * IDENTITY tail:        identity_length:u8, identity ASCII bytes (max 32)
 * CAPABILITIES tail:    capability_mask:u32
 * HEARTBEAT tail:       0..16 opaque request echo bytes
 * Error response tail:  status:u8 with PROTOCOL_FLAG_RESPONSE_ERROR set
 */

#endif
