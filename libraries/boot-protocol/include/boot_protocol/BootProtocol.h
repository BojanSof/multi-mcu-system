#ifndef BOOT_PROTOCOL_BOOT_PROTOCOL_H
#define BOOT_PROTOCOL_BOOT_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define BOOT_PROTOCOL_MAGIC_0 'M'
#define BOOT_PROTOCOL_MAGIC_1 'B'
#define BOOT_PROTOCOL_VERSION_MAJOR 1U
#define BOOT_PROTOCOL_VERSION_MINOR 0U
#define BOOT_PROTOCOL_HEADER_SIZE 20U
#define BOOT_PROTOCOL_MAX_PAYLOAD_SIZE 256U
#define BOOT_PROTOCOL_MAX_FRAME_SIZE (BOOT_PROTOCOL_HEADER_SIZE + BOOT_PROTOCOL_MAX_PAYLOAD_SIZE)
#define BOOT_PROTOCOL_NO_SLOT 0xFFU
#define BOOT_PROTOCOL_FLAG_RESPONSE 0x8000U
#define BOOT_PROTOCOL_FLAG_SLOT_MASK 0x0001U
#define BOOT_PROTOCOL_QUERY_RESPONSE_SIZE 8U
#define BOOT_PROTOCOL_BOOT_STATE_RESPONSE_SIZE 8U

/*
 * Command payload contract (all multibyte values are little-endian):
 * - QUERY_SLOTS: empty request; response payload is
 *   [valid mask, confirmed slot, selected slot, state, active slot,
 *    trial attempts, reserved, reserved].
 * - ERASE_INACTIVE: empty request; flags[0] selects the slot. The target must
 *   not be the confirmed slot. The response echoes the selected slot in
 *   flags[0].
 * - WRITE_CHUNK: flags[0] selects the slot, operation_offset is the byte
 *   offset from that slot's base, and payload is the exact bytes to program.
 * - FINALIZE: flags[0] selects the slot and operation_offset is the complete
 *   image size (signed header plus payload). The target verifies bounds,
 *   digest, signature, key id and link address before returning OK.
 * - SET_PENDING: empty payload; flags[0] selects a previously finalized slot.
 * - GET_BOOT_STATE: empty request; response uses the QUERY_SLOTS payload.
 * - REBOOT: empty payload. The response is sent before reset.
 * Responses set BOOT_PROTOCOL_FLAG_RESPONSE and echo command/sequence.
 */

typedef enum {
    BOOT_PROTOCOL_COMMAND_QUERY_SLOTS = 1,
    BOOT_PROTOCOL_COMMAND_ERASE_INACTIVE = 2,
    BOOT_PROTOCOL_COMMAND_WRITE_CHUNK = 3,
    BOOT_PROTOCOL_COMMAND_FINALIZE = 4,
    BOOT_PROTOCOL_COMMAND_SET_PENDING = 5,
    BOOT_PROTOCOL_COMMAND_GET_BOOT_STATE = 6,
    BOOT_PROTOCOL_COMMAND_REBOOT = 7
} BootProtocol_Command;

typedef enum {
    BOOT_PROTOCOL_STATUS_OK = 0,
    BOOT_PROTOCOL_STATUS_INVALID_ARGUMENT = 1,
    BOOT_PROTOCOL_STATUS_BAD_FRAME = 2,
    BOOT_PROTOCOL_STATUS_BAD_CRC = 3,
    BOOT_PROTOCOL_STATUS_UNSUPPORTED = 4,
    BOOT_PROTOCOL_STATUS_SEQUENCE = 5,
    BOOT_PROTOCOL_STATUS_STATE = 6,
    BOOT_PROTOCOL_STATUS_BOUNDARY = 7,
    BOOT_PROTOCOL_STATUS_FLASH = 8,
    BOOT_PROTOCOL_STATUS_IMAGE = 9,
    BOOT_PROTOCOL_STATUS_SIGNATURE = 10,
    BOOT_PROTOCOL_STATUS_BUSY = 11
} BootProtocol_Status;

typedef struct {
    uint8_t command;
    uint8_t status;
    uint16_t sequence;
    uint16_t payload_length;
    uint16_t flags;
    uint32_t operation_offset;
    uint8_t payload[BOOT_PROTOCOL_MAX_PAYLOAD_SIZE];
} BootProtocol_Message;

BootProtocol_Status BootProtocol_Encode(const BootProtocol_Message *message, uint8_t *frame,
                                        size_t frame_capacity, size_t *frame_size);
BootProtocol_Status BootProtocol_Decode(const uint8_t *frame, size_t frame_size,
                                        BootProtocol_Message *message);

#endif
