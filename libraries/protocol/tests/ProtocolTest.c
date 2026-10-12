#include "protocol/Protocol.h"
#include "protocol/ProtocolFrame.h"
#include "protocol/ProtocolTransport.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            (void)fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                          \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

typedef struct {
    unsigned int frames;
    unsigned int errors;
    Protocol_Status error;
    Protocol_Frame frame;
} Test_State;

static const uint8_t GOLDEN[] = {0x05U, 0x43U, 0x4DU, 0x01U, 0x01U, 0x04U, 0x34U, 0x12U, 0x03U,
                                 0x01U, 0x07U, 0x11U, 0x22U, 0x0FU, 0xB2U, 0x66U, 0x0AU, 0x00U};

static Protocol_Frame Test_Frame(void) {
    Protocol_Frame frame = {0};
    frame.kind = PROTOCOL_FRAME_REQUEST;
    frame.sequence = 0x1234U;
    frame.payload_length = 3U;
    frame.payload[1] = 0x11U;
    frame.payload[2] = 0x22U;
    return frame;
}

static void Test_OnFrame(const Protocol_Frame *frame, void *context) {
    Test_State *state = (Test_State *)context;
    ++state->frames;
    state->frame = *frame;
}

static void Test_OnError(Protocol_Status error, void *context) {
    Test_State *state = (Test_State *)context;
    ++state->errors;
    state->error = error;
}

static int Test_GoldenAndLimits(void) {
    Protocol_Frame input = Test_Frame();
    Protocol_Frame output = {0};
    uint8_t encoded[PROTOCOL_MAX_ENCODED_FRAME];
    size_t length = 0U;
    size_t index;

    CHECK(Protocol_CalculateCrc32((const uint8_t *)"123456789", 9U) == 0xCBF43926UL);
    CHECK(Protocol_EncodeFrame(&input, encoded, sizeof(encoded), &length) == PROTOCOL_STATUS_OK);
    CHECK(length == sizeof(GOLDEN));
    CHECK(memcmp(encoded, GOLDEN, sizeof(GOLDEN)) == 0);
    CHECK(Protocol_DecodeFrame(encoded, length, &output) == PROTOCOL_STATUS_OK);
    CHECK(Protocol_DecodeFrame(encoded, length - 1U, &output) == PROTOCOL_STATUS_OK);
    CHECK(output.sequence == input.sequence && output.payload_length == input.payload_length);
    CHECK(memcmp(output.payload, input.payload, input.payload_length) == 0);

    input.kind = PROTOCOL_FRAME_RESPONSE;
    input.flags = PROTOCOL_FLAG_RESPONSE_ERROR;
    input.payload_length = PROTOCOL_MAX_PAYLOAD;
    for (index = 0U; index < input.payload_length; ++index) {
        input.payload[index] = (uint8_t)(index + 1U);
    }
    CHECK(Protocol_EncodeFrame(&input, encoded, sizeof(encoded), &length) == PROTOCOL_STATUS_OK);
    CHECK(length <= PROTOCOL_MAX_ENCODED_FRAME);
    CHECK(Protocol_DecodeFrame(encoded, length, &output) == PROTOCOL_STATUS_OK);
    CHECK(memcmp(output.payload, input.payload, input.payload_length) == 0);
    CHECK(Protocol_EncodeFrame(&input, encoded, 1U, &length) == PROTOCOL_STATUS_OUTPUT_TOO_SMALL);
    input.kind = PROTOCOL_FRAME_REQUEST;
    CHECK(Protocol_EncodeFrame(&input, encoded, sizeof(encoded), &length) ==
          PROTOCOL_STATUS_INVALID_FLAGS);
    input.kind = (Protocol_FrameKind)0;
    CHECK(Protocol_EncodeFrame(&input, encoded, sizeof(encoded), &length) ==
          PROTOCOL_STATUS_INVALID_KIND);
    input.kind = PROTOCOL_FRAME_REQUEST;
    input.flags = 0x80U;
    CHECK(Protocol_EncodeFrame(&input, encoded, sizeof(encoded), &length) ==
          PROTOCOL_STATUS_INVALID_FLAGS);
    input.flags = 0U;
    input.payload_length = PROTOCOL_MAX_PAYLOAD + 1U;
    CHECK(Protocol_EncodeFrame(&input, encoded, sizeof(encoded), &length) ==
          PROTOCOL_STATUS_FRAME_TOO_LARGE);
    CHECK(Protocol_EncodeFrame(NULL, encoded, sizeof(encoded), &length) ==
          PROTOCOL_STATUS_INVALID_ARGUMENT);
    return 0;
}

static Protocol_Status Test_Mutation(size_t offset, uint8_t value) {
    Protocol_Frame frame;
    uint8_t encoded[sizeof(GOLDEN)];
    (void)memcpy(encoded, GOLDEN, sizeof(encoded));
    encoded[offset] = value;
    return Protocol_DecodeFrame(encoded, sizeof(encoded), &frame);
}

static int Test_Malformed(void) {
    Protocol_Frame frame;
    Protocol_Frame flagged = Test_Frame();
    static const uint8_t bad_run[] = {0x05U, 0x11U, 0x22U};
    uint8_t flagged_encoded[PROTOCOL_MAX_ENCODED_FRAME];
    uint8_t corrupt[sizeof(GOLDEN)];
    size_t flagged_length = 0U;
    CHECK(Protocol_DecodeFrame(bad_run, sizeof(bad_run), &frame) == PROTOCOL_STATUS_MALFORMED_COBS);
    CHECK(Test_Mutation(1U, 0x42U) == PROTOCOL_STATUS_BAD_MAGIC);
    CHECK(Test_Mutation(3U, 0x02U) == PROTOCOL_STATUS_UNSUPPORTED_VERSION);
    CHECK(Test_Mutation(4U, 0x09U) == PROTOCOL_STATUS_INVALID_KIND);
    flagged.kind = PROTOCOL_FRAME_RESPONSE;
    flagged.flags = PROTOCOL_FLAG_RESPONSE_ERROR;
    CHECK(Protocol_EncodeFrame(&flagged, flagged_encoded, sizeof(flagged_encoded),
                               &flagged_length) == PROTOCOL_STATUS_OK);
    flagged_encoded[5] = 0x80U;
    CHECK(Protocol_DecodeFrame(flagged_encoded, flagged_length, &frame) ==
          PROTOCOL_STATUS_INVALID_FLAGS);
    CHECK(Test_Mutation(8U, 0x04U) == PROTOCOL_STATUS_LENGTH_MISMATCH);
    (void)memcpy(corrupt, GOLDEN, sizeof(corrupt));
    corrupt[11] ^= 1U;
    CHECK(Protocol_DecodeFrame(corrupt, sizeof(corrupt), &frame) == PROTOCOL_STATUS_BAD_CRC);
    CHECK(Protocol_DecodeFrame(GOLDEN, 1U, &frame) == PROTOCOL_STATUS_MALFORMED_COBS);
    return 0;
}

static int Test_Stream(void) {
    Protocol_Decoder decoder;
    Test_State state = {0};
    uint8_t stream[sizeof(GOLDEN) * 2U];
    uint8_t overflow[PROTOCOL_MAX_ENCODED_FRAME + 1U + sizeof(GOLDEN)];
    size_t index;

    Protocol_DecoderInitialize(&decoder);
    for (index = 0U; index < sizeof(GOLDEN); ++index) {
        CHECK(Protocol_ConsumeBytes(&decoder, &GOLDEN[index], 1U, Test_OnFrame, Test_OnError,
                                    &state) == PROTOCOL_STATUS_OK);
    }
    CHECK(state.frames == 1U && decoder.accepted_frames == 1U);

    (void)memcpy(stream, GOLDEN, sizeof(GOLDEN));
    stream[11] ^= 1U;
    (void)memcpy(&stream[sizeof(GOLDEN)], GOLDEN, sizeof(GOLDEN));
    CHECK(Protocol_ConsumeBytes(&decoder, stream, sizeof(stream), Test_OnFrame, Test_OnError,
                                &state) == PROTOCOL_STATUS_BAD_CRC);
    CHECK(state.frames == 2U && state.errors == 1U);
    CHECK(decoder.accepted_frames == 2U && decoder.rejected_frames == 1U);

    (void)memset(overflow, 1, PROTOCOL_MAX_ENCODED_FRAME);
    overflow[PROTOCOL_MAX_ENCODED_FRAME] = 0U;
    (void)memcpy(&overflow[PROTOCOL_MAX_ENCODED_FRAME + 1U], GOLDEN, sizeof(GOLDEN));
    CHECK(Protocol_ConsumeBytes(&decoder, overflow, sizeof(overflow), Test_OnFrame, Test_OnError,
                                &state) == PROTOCOL_STATUS_FRAME_TOO_LARGE);
    CHECK(decoder.overflow_frames == 1U && decoder.rejected_frames == 2U);
    CHECK(decoder.accepted_frames == 3U && state.frames == 3U && state.errors == 2U);
    CHECK(decoder.dropping_frame == 0U && decoder.encoded_length == 0U);
    return 0;
}

static int Test_DeterministicRoundTrips(void) {
    static const size_t lengths[] = {0U, 1U, 2U, 31U, 127U, PROTOCOL_MAX_PAYLOAD};
    size_t case_index;

    for (case_index = 0U; case_index < sizeof(lengths) / sizeof(lengths[0]); ++case_index) {
        Protocol_Decoder decoder;
        Protocol_Frame input = {0};
        Test_State state = {0};
        uint8_t encoded[PROTOCOL_MAX_ENCODED_FRAME];
        size_t encoded_length = 0U;
        size_t offset = 0U;
        size_t index;

        input.kind = (case_index & 1U) != 0U ? PROTOCOL_FRAME_RESPONSE : PROTOCOL_FRAME_REQUEST;
        input.sequence = (uint16_t)(0x2000U + case_index);
        input.payload_length = (uint16_t)lengths[case_index];
        for (index = 0U; index < lengths[case_index]; ++index) {
            input.payload[index] = (uint8_t)((index * 37U + case_index) & 0xFFU);
        }
        CHECK(Protocol_EncodeFrame(&input, encoded, sizeof(encoded), &encoded_length) ==
              PROTOCOL_STATUS_OK);
        Protocol_DecoderInitialize(&decoder);
        while (offset < encoded_length) {
            size_t chunk = 1U + ((offset + case_index) % 11U);
            if (chunk > encoded_length - offset) {
                chunk = encoded_length - offset;
            }
            CHECK(Protocol_ConsumeBytes(&decoder, &encoded[offset], chunk, Test_OnFrame,
                                        Test_OnError, &state) == PROTOCOL_STATUS_OK);
            offset += chunk;
        }
        CHECK(state.frames == 1U && state.errors == 0U);
        CHECK(state.frame.kind == input.kind && state.frame.sequence == input.sequence);
        CHECK(state.frame.payload_length == input.payload_length);
        CHECK(memcmp(state.frame.payload, input.payload, input.payload_length) == 0);
    }
    return 0;
}

int main(void) {
    const Protocol_Version version = Protocol_GetVersion();
    CHECK(version.major == PROTOCOL_API_VERSION_MAJOR);
    CHECK(Test_GoldenAndLimits() == 0);
    CHECK(Test_Malformed() == 0);
    CHECK(Test_Stream() == 0);
    CHECK(Test_DeterministicRoundTrips() == 0);
    return 0;
}
