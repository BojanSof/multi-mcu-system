#ifndef NODE_TRANSPORT_H
#define NODE_TRANSPORT_H

#include <stdint.h>

typedef struct {
    uint32_t session_generation;
    uint32_t accepted_requests;
    uint32_t rejected_requests;
} NodeTransport_State;

void NodeTransport_Initialize(NodeTransport_State *state, uint32_t session_generation);
void NodeTransport_Process(NodeTransport_State *state);

#endif
