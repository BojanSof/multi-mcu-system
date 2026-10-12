#include "protocol/Protocol.h"

#include <string.h>

#include "protocol/ProtocolFrame.h"
#include "protocol/ProtocolTransport.h"

#define PROTOCOL_CRC32_POLYNOMIAL 0xEDB88320UL
#define PROTOCOL_COBS_MAX_CODE 0xFFU

static void Protocol_WriteUint16(uint8_t *output, uint16_t value) {
    output[0] = (uint8_t)(value & 0xFFU);
    output[1] = (uint8_t)(value >> 8U);
}

static void Protocol_WriteUint32(uint8_t *output, uint32_t value) {
    output[0] = (uint8_t)(value & 0xFFUL);
    output[1] = (uint8_t)((value >> 8U) & 0xFFUL);
    output[2] = (uint8_t)((value >> 16U) & 0xFFUL);
    output[3] = (uint8_t)(value >> 24U);
}

static uint16_t Protocol_ReadUint16(const uint8_t *input) {
    return (uint16_t)((uint16_t)input[0] | ((uint16_t)input[1] << 8U));
}

static uint32_t Protocol_ReadUint32(const uint8_t *input) {
    return (uint32_t)input[0] | ((uint32_t)input[1] << 8U) | ((uint32_t)input[2] << 16U) |
           ((uint32_t)input[3] << 24U);
}

static int Protocol_IsValidKind(uint8_t kind) {
    return kind == (uint8_t)PROTOCOL_FRAME_REQUEST || kind == (uint8_t)PROTOCOL_FRAME_RESPONSE ||
           kind == (uint8_t)PROTOCOL_FRAME_EVENT;
}

static int Protocol_AreValidFlags(uint8_t kind, uint8_t flags) {
    if ((flags & (uint8_t)(~PROTOCOL_ALLOWED_FLAGS)) != 0U) {
        return 0;
    }
    return (flags & PROTOCOL_FLAG_RESPONSE_ERROR) == 0U || kind == (uint8_t)PROTOCOL_FRAME_RESPONSE;
}

static Protocol_Status Protocol_CobsEncode(const uint8_t *input, size_t input_length,
                                           uint8_t *output, size_t output_capacity,
                                           size_t *output_length) {
    size_t read_index = 0U;
    size_t write_index = 1U;
    size_t code_index = 0U;
    uint8_t code = 1U;

    if (output_capacity == 0U) {
        return PROTOCOL_STATUS_OUTPUT_TOO_SMALL;
    }
    while (read_index < input_length) {
        if (input[read_index] == 0U) {
            output[code_index] = code;
            code_index = write_index;
            if (write_index >= output_capacity) {
                return PROTOCOL_STATUS_OUTPUT_TOO_SMALL;
            }
            ++write_index;
            code = 1U;
        } else {
            if (write_index >= output_capacity) {
                return PROTOCOL_STATUS_OUTPUT_TOO_SMALL;
            }
            output[write_index] = input[read_index];
            ++write_index;
            ++code;
            if (code == PROTOCOL_COBS_MAX_CODE) {
                output[code_index] = code;
                code_index = write_index;
                if (write_index >= output_capacity) {
                    return PROTOCOL_STATUS_OUTPUT_TOO_SMALL;
                }
                ++write_index;
                code = 1U;
            }
        }
        ++read_index;
    }
    output[code_index] = code;
    *output_length = write_index;
    return PROTOCOL_STATUS_OK;
}

static Protocol_Status Protocol_CobsDecode(const uint8_t *input, size_t input_length,
                                           uint8_t *output, size_t output_capacity,
                                           size_t *output_length) {
    size_t read_index = 0U;
    size_t write_index = 0U;

    if (input_length == 0U) {
        return PROTOCOL_STATUS_MALFORMED_COBS;
    }
    while (read_index < input_length) {
        const uint8_t code = input[read_index];
        size_t copy_length;
        ++read_index;
        if (code == 0U) {
            return PROTOCOL_STATUS_MALFORMED_COBS;
        }
        copy_length = (size_t)code - 1U;
        if (copy_length > (input_length - read_index)) {
            return PROTOCOL_STATUS_MALFORMED_COBS;
        }
        if (copy_length > (output_capacity - write_index)) {
            return PROTOCOL_STATUS_FRAME_TOO_LARGE;
        }
        if (copy_length > 0U) {
            (void)memcpy(&output[write_index], &input[read_index], copy_length);
            read_index += copy_length;
            write_index += copy_length;
        }
        if (code != PROTOCOL_COBS_MAX_CODE && read_index < input_length) {
            if (write_index >= output_capacity) {
                return PROTOCOL_STATUS_FRAME_TOO_LARGE;
            }
            output[write_index] = 0U;
            ++write_index;
        }
    }
    *output_length = write_index;
    return PROTOCOL_STATUS_OK;
}

Protocol_Version Protocol_GetVersion(void) {
    const Protocol_Version version = {PROTOCOL_API_VERSION_MAJOR, PROTOCOL_API_VERSION_MINOR};
    return version;
}

uint32_t Protocol_CalculateCrc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFFUL;
    size_t index;

    if (data == NULL && length != 0U) {
        return 0U;
    }
    for (index = 0U; index < length; ++index) {
        uint8_t bit;
        crc ^= data[index];
        for (bit = 0U; bit < 8U; ++bit) {
            const uint32_t mask = (uint32_t)(0U - (crc & 1U));
            crc = (crc >> 1U) ^ (PROTOCOL_CRC32_POLYNOMIAL & mask);
        }
    }
    return crc ^ 0xFFFFFFFFUL;
}

Protocol_Status Protocol_EncodeFrame(const Protocol_Frame *frame, uint8_t *output,
                                     size_t output_capacity, size_t *output_length) {
    uint8_t decoded[PROTOCOL_MAX_DECODED_FRAME];
    size_t decoded_length;
    size_t encoded_length;
    uint32_t crc;
    Protocol_Status status;

    if (output_length != NULL) {
        *output_length = 0U;
    }
    if (frame == NULL || output == NULL || output_length == NULL) {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }
    if (!Protocol_IsValidKind((uint8_t)frame->kind)) {
        return PROTOCOL_STATUS_INVALID_KIND;
    }
    if (!Protocol_AreValidFlags((uint8_t)frame->kind, frame->flags)) {
        return PROTOCOL_STATUS_INVALID_FLAGS;
    }
    if (frame->payload_length > PROTOCOL_MAX_PAYLOAD) {
        return PROTOCOL_STATUS_FRAME_TOO_LARGE;
    }
    Protocol_WriteUint16(&decoded[0], PROTOCOL_MAGIC);
    decoded[2] = PROTOCOL_WIRE_VERSION_MAJOR;
    decoded[3] = (uint8_t)frame->kind;
    decoded[4] = frame->flags;
    Protocol_WriteUint16(&decoded[5], frame->sequence);
    Protocol_WriteUint16(&decoded[7], frame->payload_length);
    if (frame->payload_length > 0U) {
        (void)memcpy(&decoded[PROTOCOL_HEADER_SIZE], frame->payload, frame->payload_length);
    }
    decoded_length = PROTOCOL_HEADER_SIZE + frame->payload_length;
    crc = Protocol_CalculateCrc32(decoded, decoded_length);
    Protocol_WriteUint32(&decoded[decoded_length], crc);
    decoded_length += PROTOCOL_CRC_SIZE;

    if (output_capacity == 0U) {
        return PROTOCOL_STATUS_OUTPUT_TOO_SMALL;
    }
    status =
        Protocol_CobsEncode(decoded, decoded_length, output, output_capacity - 1U, &encoded_length);
    if (status != PROTOCOL_STATUS_OK) {
        return status;
    }
    if (encoded_length >= output_capacity) {
        return PROTOCOL_STATUS_OUTPUT_TOO_SMALL;
    }
    output[encoded_length] = 0U;
    *output_length = encoded_length + 1U;
    return PROTOCOL_STATUS_OK;
}

Protocol_Status Protocol_DecodeFrame(const uint8_t *encoded, size_t encoded_length,
                                     Protocol_Frame *frame) {
    uint8_t decoded[PROTOCOL_MAX_DECODED_FRAME];
    size_t decoded_length = 0U;
    uint16_t payload_length;
    size_t expected_length;
    uint32_t expected_crc;
    uint32_t actual_crc;
    Protocol_Status status;

    if (encoded == NULL || frame == NULL) {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }
    if (encoded_length > 0U && encoded[encoded_length - 1U] == 0U) {
        --encoded_length;
    }
    status =
        Protocol_CobsDecode(encoded, encoded_length, decoded, sizeof(decoded), &decoded_length);
    if (status != PROTOCOL_STATUS_OK) {
        return status;
    }
    if (decoded_length < (PROTOCOL_HEADER_SIZE + PROTOCOL_CRC_SIZE)) {
        return PROTOCOL_STATUS_LENGTH_MISMATCH;
    }
    if (Protocol_ReadUint16(&decoded[0]) != PROTOCOL_MAGIC) {
        return PROTOCOL_STATUS_BAD_MAGIC;
    }
    if (decoded[2] != PROTOCOL_WIRE_VERSION_MAJOR) {
        return PROTOCOL_STATUS_UNSUPPORTED_VERSION;
    }
    if (!Protocol_IsValidKind(decoded[3])) {
        return PROTOCOL_STATUS_INVALID_KIND;
    }
    if (!Protocol_AreValidFlags(decoded[3], decoded[4])) {
        return PROTOCOL_STATUS_INVALID_FLAGS;
    }
    payload_length = Protocol_ReadUint16(&decoded[7]);
    if (payload_length > PROTOCOL_MAX_PAYLOAD) {
        return PROTOCOL_STATUS_FRAME_TOO_LARGE;
    }
    expected_length = PROTOCOL_HEADER_SIZE + payload_length + PROTOCOL_CRC_SIZE;
    if (decoded_length != expected_length) {
        return PROTOCOL_STATUS_LENGTH_MISMATCH;
    }
    expected_crc = Protocol_ReadUint32(&decoded[expected_length - PROTOCOL_CRC_SIZE]);
    actual_crc = Protocol_CalculateCrc32(decoded, expected_length - PROTOCOL_CRC_SIZE);
    if (expected_crc != actual_crc) {
        return PROTOCOL_STATUS_BAD_CRC;
    }
    frame->kind = (Protocol_FrameKind)decoded[3];
    frame->flags = decoded[4];
    frame->sequence = Protocol_ReadUint16(&decoded[5]);
    frame->payload_length = payload_length;
    if (payload_length > 0U) {
        (void)memcpy(frame->payload, &decoded[PROTOCOL_HEADER_SIZE], payload_length);
    }
    return PROTOCOL_STATUS_OK;
}

void Protocol_DecoderInitialize(Protocol_Decoder *decoder) {
    if (decoder != NULL) {
        (void)memset(decoder, 0, sizeof(*decoder));
    }
}

Protocol_Status Protocol_ConsumeBytes(Protocol_Decoder *decoder, const uint8_t *bytes,
                                      size_t length, Protocol_FrameCallback frame_callback,
                                      Protocol_ErrorCallback error_callback, void *context) {
    Protocol_Status result = PROTOCOL_STATUS_OK;
    size_t index;

    if (decoder == NULL || (bytes == NULL && length != 0U)) {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0U; index < length; ++index) {
        if (bytes[index] != 0U) {
            if (decoder->dropping_frame == 0U) {
                if (decoder->encoded_length < sizeof(decoder->encoded)) {
                    decoder->encoded[decoder->encoded_length] = bytes[index];
                    ++decoder->encoded_length;
                } else {
                    decoder->dropping_frame = 1U;
                    ++decoder->overflow_frames;
                }
            }
        } else if (decoder->dropping_frame != 0U) {
            decoder->dropping_frame = 0U;
            decoder->encoded_length = 0U;
            ++decoder->rejected_frames;
            if (result == PROTOCOL_STATUS_OK) {
                result = PROTOCOL_STATUS_FRAME_TOO_LARGE;
            }
            if (error_callback != NULL) {
                error_callback(PROTOCOL_STATUS_FRAME_TOO_LARGE, context);
            }
        } else {
            Protocol_Frame frame;
            const Protocol_Status status =
                Protocol_DecodeFrame(decoder->encoded, decoder->encoded_length, &frame);
            decoder->encoded_length = 0U;
            if (status == PROTOCOL_STATUS_OK) {
                ++decoder->accepted_frames;
                if (frame_callback != NULL) {
                    frame_callback(&frame, context);
                }
            } else {
                ++decoder->rejected_frames;
                if (result == PROTOCOL_STATUS_OK) {
                    result = status;
                }
                if (error_callback != NULL) {
                    error_callback(status, context);
                }
            }
        }
    }
    return result;
}
