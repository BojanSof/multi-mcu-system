#ifndef NODE_API_RELIABLE_EVENTS_H
#define NODE_API_RELIABLE_EVENTS_H

#include "node_api/NodeServices.h"

#define NODE_EVENTS_CAPACITY 16U
#define NODE_EVENTS_RETRY_MS 250U
#define NODE_EVENTS_MAX_SENDS 5U

typedef struct {
    uint32_t id;
    uint16_t service;
    uint16_t event;
    uint32_t operation;
    uint8_t data[NODE_API_SERVICE_DATA_MAX_LENGTH];
    size_t length;
} NodeEvents_Event;

typedef struct {
    NodeEvents_Event entries[NODE_EVENTS_CAPACITY];
    size_t head;
    size_t count;
    uint32_t next_id;
    uint32_t last_sent_ms;
    uint32_t overflows;
    uint32_t retries;
    uint32_t exhausted;
    uint8_t sends;
    bool exhaustion_counted;
} NodeEvents_Queue;

void NodeEvents_Initialize(NodeEvents_Queue *queue);
/* Explicit renegotiation rearms delivery without discarding retained entries. */
void NodeEvents_Rearm(NodeEvents_Queue *queue);
NodeApi_Status NodeEvents_Push(NodeEvents_Queue *queue, uint16_t service, uint16_t event,
                               uint32_t operation, const uint8_t *data, size_t length);
const NodeEvents_Event *NodeEvents_Due(NodeEvents_Queue *queue, uint32_t now_ms);
void NodeEvents_Sent(NodeEvents_Queue *queue, uint32_t now_ms);
bool NodeEvents_Acknowledge(NodeEvents_Queue *queue, uint32_t id);
uint32_t NodeEvents_FirstId(const NodeEvents_Queue *queue);

#endif
