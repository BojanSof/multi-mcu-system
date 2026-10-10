#ifndef NODE_API_NODE_API_H
#define NODE_API_NODE_API_H

#include <stdint.h>

#define NODE_API_VERSION_MAJOR 1U
#define NODE_API_VERSION_MINOR 0U

typedef struct {
    uint16_t major;
    uint16_t minor;
} NodeApi_Version;

NodeApi_Version NodeApi_GetVersion(void);

#endif
