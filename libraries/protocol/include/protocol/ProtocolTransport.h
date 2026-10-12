#ifndef PROTOCOL_PROTOCOL_TRANSPORT_H
#define PROTOCOL_PROTOCOL_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#include "protocol/ProtocolTypes.h"

typedef void (*Protocol_FrameCallback)(const Protocol_Frame *frame, void *context);
typedef void (*Protocol_ErrorCallback)(Protocol_Status status, void *context);

typedef struct {
    uint8_t encoded[PROTOCOL_MAX_ENCODED_FRAME - 1U];
    size_t encoded_length;
    uint32_t accepted_frames;
    uint32_t rejected_frames;
    uint32_t overflow_frames;
    uint8_t dropping_frame;
} Protocol_Decoder;

void Protocol_DecoderInitialize(Protocol_Decoder *decoder);
Protocol_Status Protocol_ConsumeBytes(Protocol_Decoder *decoder, const uint8_t *bytes,
                                      size_t length, Protocol_FrameCallback frame_callback,
                                      Protocol_ErrorCallback error_callback, void *context);

#endif
