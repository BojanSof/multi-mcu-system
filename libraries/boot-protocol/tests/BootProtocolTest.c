#include "boot_protocol/BootProtocol.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            (void)fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #condition);    \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static BootProtocol_Message BootProtocolTest_Message(uint16_t payload_length) {
    BootProtocol_Message message = {
        .command = BOOT_PROTOCOL_COMMAND_WRITE_CHUNK,
        .status = BOOT_PROTOCOL_STATUS_OK,
        .sequence = UINT16_C(0x1234),
        .payload_length = payload_length,
        .flags = UINT16_C(0x5678),
        .operation_offset = UINT32_C(0x89ABCDEF),
    };
    uint16_t index;

    for (index = 0U; index < payload_length; ++index) {
        message.payload[index] = (uint8_t)(index ^ UINT16_C(0xA5));
    }
    return message;
}

static int BootProtocolTest_RoundTrip(void) {
    uint8_t frame[BOOT_PROTOCOL_MAX_FRAME_SIZE];
    size_t frame_size = 0U;
    BootProtocol_Message input = BootProtocolTest_Message(3U);
    BootProtocol_Message output;

    CHECK(BootProtocol_Encode(&input, frame, sizeof(frame), &frame_size) ==
          BOOT_PROTOCOL_STATUS_OK);
    CHECK(frame_size == BOOT_PROTOCOL_HEADER_SIZE + 3U);
    CHECK(frame[0] == 'M' && frame[1] == 'B');
    CHECK(frame[6] == 0x34U && frame[7] == 0x12U);
    CHECK(frame[12] == 0xEFU && frame[15] == 0x89U);
    CHECK(BootProtocol_Decode(frame, frame_size, &output) == BOOT_PROTOCOL_STATUS_OK);
    CHECK(output.command == input.command && output.status == input.status);
    CHECK(output.sequence == input.sequence && output.flags == input.flags);
    CHECK(output.operation_offset == input.operation_offset);
    CHECK(output.payload_length == input.payload_length);
    CHECK(memcmp(output.payload, input.payload, input.payload_length) == 0);
    return 0;
}

static int BootProtocolTest_Boundaries(void) {
    uint8_t frame[BOOT_PROTOCOL_MAX_FRAME_SIZE];
    size_t frame_size = 99U;
    BootProtocol_Message input = BootProtocolTest_Message(BOOT_PROTOCOL_MAX_PAYLOAD_SIZE);
    BootProtocol_Message output;

    CHECK(BootProtocol_Encode(&input, frame, sizeof(frame), &frame_size) ==
          BOOT_PROTOCOL_STATUS_OK);
    CHECK(frame_size == sizeof(frame));
    CHECK(BootProtocol_Decode(frame, frame_size, &output) == BOOT_PROTOCOL_STATUS_OK);
    CHECK(memcmp(output.payload, input.payload, sizeof(input.payload)) == 0);
    CHECK(BootProtocol_Encode(&input, frame, sizeof(frame) - 1U, &frame_size) ==
          BOOT_PROTOCOL_STATUS_BOUNDARY);
    CHECK(frame_size == 0U);
    input.payload_length = BOOT_PROTOCOL_MAX_PAYLOAD_SIZE + 1U;
    CHECK(BootProtocol_Encode(&input, frame, sizeof(frame), &frame_size) ==
          BOOT_PROTOCOL_STATUS_BOUNDARY);
    frame[8] = 1U;
    frame[9] = 1U;
    CHECK(BootProtocol_Decode(frame, sizeof(frame), &output) == BOOT_PROTOCOL_STATUS_BOUNDARY);
    return 0;
}

static int BootProtocolTest_Malformed(void) {
    uint8_t frame[BOOT_PROTOCOL_MAX_FRAME_SIZE];
    size_t frame_size = 0U;
    BootProtocol_Message input = BootProtocolTest_Message(4U);
    BootProtocol_Message output;
    size_t index;

    CHECK(BootProtocol_Encode(NULL, frame, sizeof(frame), &frame_size) ==
          BOOT_PROTOCOL_STATUS_INVALID_ARGUMENT);
    CHECK(BootProtocol_Encode(&input, NULL, sizeof(frame), &frame_size) ==
          BOOT_PROTOCOL_STATUS_INVALID_ARGUMENT);
    CHECK(BootProtocol_Encode(&input, frame, sizeof(frame), NULL) ==
          BOOT_PROTOCOL_STATUS_INVALID_ARGUMENT);
    CHECK(BootProtocol_Encode(&input, frame, sizeof(frame), &frame_size) ==
          BOOT_PROTOCOL_STATUS_OK);
    CHECK(BootProtocol_Decode(NULL, frame_size, &output) == BOOT_PROTOCOL_STATUS_INVALID_ARGUMENT);
    CHECK(BootProtocol_Decode(frame, frame_size, NULL) == BOOT_PROTOCOL_STATUS_INVALID_ARGUMENT);
    for (index = 0U; index < BOOT_PROTOCOL_HEADER_SIZE; ++index) {
        CHECK(BootProtocol_Decode(frame, index, &output) == BOOT_PROTOCOL_STATUS_BAD_FRAME);
    }
    CHECK(BootProtocol_Decode(frame, frame_size - 1U, &output) == BOOT_PROTOCOL_STATUS_BAD_FRAME);
    CHECK(BootProtocol_Decode(frame, frame_size + 1U, &output) == BOOT_PROTOCOL_STATUS_BAD_FRAME);
    frame[0] ^= 1U;
    CHECK(BootProtocol_Decode(frame, frame_size, &output) == BOOT_PROTOCOL_STATUS_BAD_FRAME);
    frame[0] ^= 1U;
    frame[2] += 1U;
    CHECK(BootProtocol_Decode(frame, frame_size, &output) == BOOT_PROTOCOL_STATUS_UNSUPPORTED);
    frame[2] = BOOT_PROTOCOL_VERSION_MAJOR;
    frame[4] = 0U;
    CHECK(BootProtocol_Decode(frame, frame_size, &output) == BOOT_PROTOCOL_STATUS_UNSUPPORTED);
    return 0;
}

static int BootProtocolTest_Crc(void) {
    uint8_t frame[BOOT_PROTOCOL_MAX_FRAME_SIZE];
    uint8_t original;
    size_t frame_size = 0U;
    BootProtocol_Message input = BootProtocolTest_Message(4U);
    BootProtocol_Message output;

    CHECK(BootProtocol_Encode(&input, frame, sizeof(frame), &frame_size) ==
          BOOT_PROTOCOL_STATUS_OK);
    original = frame[12];
    frame[12] ^= 1U;
    CHECK(BootProtocol_Decode(frame, frame_size, &output) == BOOT_PROTOCOL_STATUS_BAD_CRC);
    frame[12] = original;
    frame[BOOT_PROTOCOL_HEADER_SIZE + 2U] ^= 1U;
    CHECK(BootProtocol_Decode(frame, frame_size, &output) == BOOT_PROTOCOL_STATUS_BAD_CRC);
    frame[BOOT_PROTOCOL_HEADER_SIZE + 2U] ^= 1U;
    frame[16] ^= 1U;
    CHECK(BootProtocol_Decode(frame, frame_size, &output) == BOOT_PROTOCOL_STATUS_BAD_CRC);
    return 0;
}

static int BootProtocolTest_Empty(void) {
    uint8_t frame[BOOT_PROTOCOL_HEADER_SIZE];
    size_t frame_size = 0U;
    BootProtocol_Message input = BootProtocolTest_Message(0U);
    BootProtocol_Message output;

    input.command = BOOT_PROTOCOL_COMMAND_QUERY_SLOTS;
    input.operation_offset = 0U;
    CHECK(BootProtocol_Encode(&input, frame, sizeof(frame), &frame_size) ==
          BOOT_PROTOCOL_STATUS_OK);
    CHECK(frame_size == sizeof(frame));
    /* IEEE CRC32 over bytes 0..15; independently checked with Python zlib.crc32. */
    CHECK(frame[16] == 0x71U && frame[17] == 0x65U && frame[18] == 0x54U && frame[19] == 0x1CU);
    CHECK(BootProtocol_Decode(frame, frame_size, &output) == BOOT_PROTOCOL_STATUS_OK);
    CHECK(output.payload_length == 0U);
    return 0;
}

int main(void) {
    CHECK(BootProtocolTest_RoundTrip() == 0);
    CHECK(BootProtocolTest_Boundaries() == 0);
    CHECK(BootProtocolTest_Malformed() == 0);
    CHECK(BootProtocolTest_Crc() == 0);
    CHECK(BootProtocolTest_Empty() == 0);
    return 0;
}
