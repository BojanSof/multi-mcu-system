#ifndef NODE_TRANSPORT_H
#define NODE_TRANSPORT_H

#include <stdint.h>

#include "node_api/NodeServices.h"

typedef struct {
    uint32_t session_generation;
    uint32_t accepted_requests;
    uint32_t rejected_requests;
} NodeTransport_State;

void NodeTransport_Initialize(NodeTransport_State *state, uint32_t session_generation,
                              NodeApi_Registry *registry);
void NodeTransport_Process(NodeTransport_State *state);
NodeApi_Status NodeTransport_PublishServiceEvent(uint16_t service_id, uint16_t event_id,
                                                 uint32_t operation_id, const uint8_t *data,
                                                 size_t data_length);

void NodeTransport_GetEventStatus(NodeApi_ServiceResponse *response);

#endif
