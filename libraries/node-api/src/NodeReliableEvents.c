#include "node_api/NodeReliableEvents.h"

#include <limits.h>
#include <string.h>

static void NodeEvents_Increment(uint32_t *value) {
    if (*value != UINT32_MAX) {
        ++*value;
    }
}

void NodeEvents_Initialize(NodeEvents_Queue *queue) {
    if (queue != NULL) {
        memset(queue, 0, sizeof(*queue));
        queue->next_id = 1U;
    }
}

void NodeEvents_Rearm(NodeEvents_Queue *queue) {
    if (queue != NULL) {
        queue->sends = 0U;
        queue->exhaustion_counted = false;
    }
}

NodeApi_Status NodeEvents_Push(NodeEvents_Queue *queue, uint16_t service, uint16_t event,
                               uint32_t operation, const uint8_t *data, size_t length) {
    if (queue == NULL || length > NODE_API_SERVICE_DATA_MAX_LENGTH ||
        (length != 0U && data == NULL)) {
        return NODE_API_STATUS_INVALID_ARGUMENT;
    }
    if (queue->count == NODE_EVENTS_CAPACITY || queue->next_id == UINT32_MAX) {
        NodeEvents_Increment(&queue->overflows);
        return NODE_API_STATUS_BUSY;
    }
    NodeEvents_Event *entry = &queue->entries[(queue->head + queue->count) % NODE_EVENTS_CAPACITY];
    entry->id = queue->next_id++;
    entry->service = service;
    entry->event = event;
    entry->operation = operation;
    entry->length = length;
    if (length != 0U) {
        memcpy(entry->data, data, length);
    }
    ++queue->count;
    return NODE_API_STATUS_OK;
}

const NodeEvents_Event *NodeEvents_Due(NodeEvents_Queue *queue, uint32_t now_ms) {
    if (queue == NULL || queue->count == 0U) {
        return NULL;
    }
    if (queue->sends != 0U && (uint32_t)(now_ms - queue->last_sent_ms) < NODE_EVENTS_RETRY_MS) {
        return NULL;
    }
    if (queue->sends >= NODE_EVENTS_MAX_SENDS) {
        if (!queue->exhaustion_counted) {
            NodeEvents_Increment(&queue->exhausted);
            queue->exhaustion_counted = true;
        }
        return NULL;
    }
    return &queue->entries[queue->head];
}

void NodeEvents_Sent(NodeEvents_Queue *queue, uint32_t now_ms) {
    if (queue != NULL && queue->count != 0U && queue->sends < NODE_EVENTS_MAX_SENDS) {
        if (queue->sends != 0U) {
            NodeEvents_Increment(&queue->retries);
        }
        ++queue->sends;
        queue->last_sent_ms = now_ms;
    }
}

bool NodeEvents_Acknowledge(NodeEvents_Queue *queue, uint32_t id) {
    /* Stop-and-wait is a cumulative window of one. Never accept an unsent ID. */
    if (queue == NULL || queue->count == 0U || queue->sends == 0U ||
        queue->entries[queue->head].id != id) {
        return false;
    }
    queue->head = (queue->head + 1U) % NODE_EVENTS_CAPACITY;
    --queue->count;
    queue->sends = 0U;
    queue->exhaustion_counted = false;
    return true;
}

uint32_t NodeEvents_FirstId(const NodeEvents_Queue *queue) {
    return queue->count == 0U ? queue->next_id : queue->entries[queue->head].id;
}
