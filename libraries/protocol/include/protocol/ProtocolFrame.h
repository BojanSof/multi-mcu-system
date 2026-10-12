#ifndef PROTOCOL_PROTOCOL_FRAME_H
#define PROTOCOL_PROTOCOL_FRAME_H

#include <stddef.h>
#include <stdint.h>

#include "protocol/ProtocolTypes.h"

Protocol_Status Protocol_EncodeFrame(const Protocol_Frame *frame, uint8_t *output,
                                     size_t output_capacity, size_t *output_length);
Protocol_Status Protocol_DecodeFrame(const uint8_t *encoded, size_t encoded_length,
                                     Protocol_Frame *frame);
uint32_t Protocol_CalculateCrc32(const uint8_t *data, size_t length);

#endif
