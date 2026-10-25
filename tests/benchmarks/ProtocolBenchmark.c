#include "protocol/ProtocolFrame.h"
#include "protocol/ProtocolTransport.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define PROTOCOL_BENCHMARK_FRAMES 20000U

static void ProtocolBenchmark_Received(const Protocol_Frame *frame, void *context) {
    uint32_t *count = context;
    assert(frame->payload_length == 192U && frame->sequence == 1U);
    ++*count;
}

static int ProtocolBenchmark_Run(size_t fragment) {
    Protocol_Frame frame = {.kind = PROTOCOL_FRAME_REQUEST, .sequence = 1U, .payload_length = 192U};
    memset(frame.payload, 0x5A, frame.payload_length);
    uint8_t wire[PROTOCOL_MAX_ENCODED_FRAME];
    size_t length;
    assert(Protocol_EncodeFrame(&frame, wire, sizeof(wire), &length) == PROTOCOL_STATUS_OK);
    Protocol_Decoder decoder;
    Protocol_DecoderInitialize(&decoder);
    uint32_t received = 0;
    const clock_t start = clock();
    if (start == (clock_t)-1) {
        return 1;
    }
    for (uint32_t n = 0; n < PROTOCOL_BENCHMARK_FRAMES; ++n) {
        for (size_t offset = 0; offset < length;) {
            const size_t count = length - offset < fragment ? length - offset : fragment;
            assert(Protocol_ConsumeBytes(&decoder, wire + offset, count, ProtocolBenchmark_Received,
                                         NULL, &received) == PROTOCOL_STATUS_OK);
            offset += count;
        }
    }
    const clock_t end = clock();
    assert(received == PROTOCOL_BENCHMARK_FRAMES && decoder.rejected_frames == 0U);
    if (end == (clock_t)-1 || end <= start) {
        return 1;
    }
    const double seconds = (double)(end - start) / (double)CLOCKS_PER_SEC;
    printf("{\"mode\":\"host_native\",\"fragment_bytes\":%zu,\"frames\":%" PRIu32
           ",\"wire_bytes_per_frame\":%zu,\"cpu_seconds\":%.6f,\"frames_per_cpu_second\":%.1f}\n",
           fragment, received, length, seconds, (double)received / seconds);
    return 0;
}

int main(void) { return ProtocolBenchmark_Run(1U) != 0 || ProtocolBenchmark_Run(64U) != 0 ? 1 : 0; }
