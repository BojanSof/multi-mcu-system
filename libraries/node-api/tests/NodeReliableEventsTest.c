#include "node_api/NodeReliableEvents.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>

int main(void) {
    NodeEvents_Queue queue;
    NodeEvents_Initialize(&queue);
    assert(NodeEvents_Due(&queue, 0U) == NULL);
    assert(NodeEvents_FirstId(&queue) == 1U);
    assert(NodeEvents_Push(&queue, 1U, 2U, 3U, NULL, 1U) == NODE_API_STATUS_INVALID_ARGUMENT);
    for (size_t i = 0; i < NODE_EVENTS_CAPACITY; ++i) {
        uint8_t data = (uint8_t)i;
        assert(NodeEvents_Push(&queue, 1U, 2U, 3U, &data, 1U) == NODE_API_STATUS_OK);
    }
    assert(NodeEvents_Push(&queue, 1U, 2U, 3U, NULL, 0U) == NODE_API_STATUS_BUSY);
    assert(queue.overflows == 1U && queue.next_id == 17U);
    assert(!NodeEvents_Acknowledge(&queue, 1U));
    assert(NodeEvents_Due(&queue, 0U)->id == 1U);
    NodeEvents_Sent(&queue, 0U);
    assert(!NodeEvents_Acknowledge(&queue, 2U));
    assert(NodeEvents_Due(&queue, 249U) == NULL);
    for (uint32_t i = 1U; i < NODE_EVENTS_MAX_SENDS; ++i) {
        assert(NodeEvents_Due(&queue, i * 250U)->id == 1U);
        NodeEvents_Sent(&queue, i * 250U);
    }
    assert(NodeEvents_Due(&queue, 1250U) == NULL);
    assert(NodeEvents_Due(&queue, 1500U) == NULL);
    assert(queue.exhausted == 1U && queue.retries == 4U && queue.count == 16U);
    assert(NodeEvents_Acknowledge(&queue, 1U));
    assert(!NodeEvents_Acknowledge(&queue, 1U));
    for (uint32_t i = 2U; i <= 16U; ++i) {
        assert(NodeEvents_Due(&queue, 1500U)->id == i);
        NodeEvents_Sent(&queue, 1500U);
        assert(NodeEvents_Acknowledge(&queue, i));
    }
    assert(queue.count == 0U && NodeEvents_FirstId(&queue) == 17U);
    assert(NodeEvents_Push(&queue, 0U, 0U, 0U, NULL, 0U) == NODE_API_STATUS_OK);
    NodeEvents_Sent(&queue, UINT32_MAX - 100U);
    assert(NodeEvents_Due(&queue, 148U) == NULL);
    assert(NodeEvents_Due(&queue, 149U) != NULL);
    NodeEvents_Initialize(&queue); /* reset discards volatile history */
    assert(!NodeEvents_Acknowledge(&queue, 17U));
    queue.next_id = UINT32_MAX;
    assert(NodeEvents_Push(&queue, 0U, 0U, 0U, NULL, 0U) == NODE_API_STATUS_BUSY);
    puts("reliable queue: capacity, lost ACK, exhaustion, rollover, reset passed");
    return 0;
}
