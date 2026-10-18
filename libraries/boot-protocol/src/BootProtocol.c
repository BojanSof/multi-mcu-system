#include "boot_protocol/BootProtocol.h"

#include <stdbool.h>
#include <string.h>

#define BOOT_PROTOCOL_CRC_OFFSET 16U
#define BOOT_PROTOCOL_PAYLOAD_OFFSET BOOT_PROTOCOL_HEADER_SIZE
#define BOOT_PROTOCOL_CRC32_POLYNOMIAL UINT32_C(0xEDB88320)

static void BootProtocol_WriteU16(uint8_t *data, uint16_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
}

static void BootProtocol_WriteU32(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

static uint16_t BootProtocol_ReadU16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8U));
}

static uint32_t BootProtocol_ReadU32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) | ((uint32_t)data[2] << 16U) |
           ((uint32_t)data[3] << 24U);
}

static uint32_t BootProtocol_CrcUpdate(uint32_t crc, const uint8_t *data, size_t size) {
    size_t index;

    for (index = 0U; index < size; ++index) {
        uint8_t bit;
        crc ^= data[index];
        for (bit = 0U; bit < 8U; ++bit) {
            const uint32_t mask = (uint32_t)(0U - (crc & 1U));
            crc = (crc >> 1U) ^ (BOOT_PROTOCOL_CRC32_POLYNOMIAL & mask);
        }
    }
    return crc;
}

static uint32_t BootProtocol_CalculateCrc(const uint8_t *frame, uint16_t payload_length) {
    uint32_t crc = UINT32_MAX;
    crc = BootProtocol_CrcUpdate(crc, frame, BOOT_PROTOCOL_CRC_OFFSET);
    crc = BootProtocol_CrcUpdate(crc, &frame[BOOT_PROTOCOL_PAYLOAD_OFFSET], payload_length);
    return crc ^ UINT32_MAX;
}

static bool BootProtocol_CommandIsKnown(uint8_t command) {
    return (command >= (uint8_t)BOOT_PROTOCOL_COMMAND_QUERY_SLOTS) &&
           (command <= (uint8_t)BOOT_PROTOCOL_COMMAND_REBOOT);
}

static bool BootProtocol_StatusIsKnown(uint8_t status) {
    return status <= (uint8_t)BOOT_PROTOCOL_STATUS_BUSY;
}

BootProtocol_Status BootProtocol_Encode(const BootProtocol_Message *message, uint8_t *frame,
                                        size_t frame_capacity, size_t *frame_size) {
    size_t required_size;
    uint32_t crc;

    if (frame_size != NULL) {
        *frame_size = 0U;
    }
    if ((message == NULL) || (frame == NULL) || (frame_size == NULL)) {
        return BOOT_PROTOCOL_STATUS_INVALID_ARGUMENT;
    }
    if (message->payload_length > BOOT_PROTOCOL_MAX_PAYLOAD_SIZE) {
        return BOOT_PROTOCOL_STATUS_BOUNDARY;
    }
    if (!BootProtocol_CommandIsKnown(message->command) ||
        !BootProtocol_StatusIsKnown(message->status)) {
        return BOOT_PROTOCOL_STATUS_UNSUPPORTED;
    }
    required_size = BOOT_PROTOCOL_HEADER_SIZE + (size_t)message->payload_length;
    if (frame_capacity < required_size) {
        return BOOT_PROTOCOL_STATUS_BOUNDARY;
    }

    frame[0] = (uint8_t)BOOT_PROTOCOL_MAGIC_0;
    frame[1] = (uint8_t)BOOT_PROTOCOL_MAGIC_1;
    frame[2] = BOOT_PROTOCOL_VERSION_MAJOR;
    frame[3] = BOOT_PROTOCOL_VERSION_MINOR;
    frame[4] = message->command;
    frame[5] = message->status;
    BootProtocol_WriteU16(&frame[6], message->sequence);
    BootProtocol_WriteU16(&frame[8], message->payload_length);
    BootProtocol_WriteU16(&frame[10], message->flags);
    BootProtocol_WriteU32(&frame[12], message->operation_offset);
    if (message->payload_length > 0U) {
        (void)memcpy(&frame[BOOT_PROTOCOL_PAYLOAD_OFFSET], message->payload,
                     message->payload_length);
    }
    crc = BootProtocol_CalculateCrc(frame, message->payload_length);
    BootProtocol_WriteU32(&frame[BOOT_PROTOCOL_CRC_OFFSET], crc);
    *frame_size = required_size;
    return BOOT_PROTOCOL_STATUS_OK;
}

BootProtocol_Status BootProtocol_Decode(const uint8_t *frame, size_t frame_size,
                                        BootProtocol_Message *message) {
    uint16_t payload_length;
    size_t expected_size;
    uint32_t expected_crc;

    if ((frame == NULL) || (message == NULL)) {
        return BOOT_PROTOCOL_STATUS_INVALID_ARGUMENT;
    }
    (void)memset(message, 0, sizeof(*message));
    if (frame_size < BOOT_PROTOCOL_HEADER_SIZE) {
        return BOOT_PROTOCOL_STATUS_BAD_FRAME;
    }
    if ((frame[0] != (uint8_t)BOOT_PROTOCOL_MAGIC_0) ||
        (frame[1] != (uint8_t)BOOT_PROTOCOL_MAGIC_1)) {
        return BOOT_PROTOCOL_STATUS_BAD_FRAME;
    }
    if ((frame[2] != BOOT_PROTOCOL_VERSION_MAJOR) || (frame[3] != BOOT_PROTOCOL_VERSION_MINOR) ||
        !BootProtocol_CommandIsKnown(frame[4]) || !BootProtocol_StatusIsKnown(frame[5])) {
        return BOOT_PROTOCOL_STATUS_UNSUPPORTED;
    }
    payload_length = BootProtocol_ReadU16(&frame[8]);
    if (payload_length > BOOT_PROTOCOL_MAX_PAYLOAD_SIZE) {
        return BOOT_PROTOCOL_STATUS_BOUNDARY;
    }
    expected_size = BOOT_PROTOCOL_HEADER_SIZE + (size_t)payload_length;
    if (frame_size != expected_size) {
        return BOOT_PROTOCOL_STATUS_BAD_FRAME;
    }
    expected_crc = BootProtocol_ReadU32(&frame[BOOT_PROTOCOL_CRC_OFFSET]);
    if (expected_crc != BootProtocol_CalculateCrc(frame, payload_length)) {
        return BOOT_PROTOCOL_STATUS_BAD_CRC;
    }

    message->command = frame[4];
    message->status = frame[5];
    message->sequence = BootProtocol_ReadU16(&frame[6]);
    message->payload_length = payload_length;
    message->flags = BootProtocol_ReadU16(&frame[10]);
    message->operation_offset = BootProtocol_ReadU32(&frame[12]);
    if (payload_length > 0U) {
        (void)memcpy(message->payload, &frame[BOOT_PROTOCOL_PAYLOAD_OFFSET], payload_length);
    }
    return BOOT_PROTOCOL_STATUS_OK;
}
