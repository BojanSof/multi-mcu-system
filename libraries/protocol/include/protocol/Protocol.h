#ifndef PROTOCOL_PROTOCOL_H
#define PROTOCOL_PROTOCOL_H

#include <stdint.h>

#define PROTOCOL_API_VERSION_MAJOR 1U
#define PROTOCOL_API_VERSION_MINOR 0U

typedef struct {
    uint16_t major;
    uint16_t minor;
} Protocol_Version;

Protocol_Version Protocol_GetVersion(void);

#endif
