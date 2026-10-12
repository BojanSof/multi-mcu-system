#ifndef HOST_NODE_MANAGER_H
#define HOST_NODE_MANAGER_H

#include <stdint.h>

typedef enum {
    HOST_LINK_DISABLED = 0,
    HOST_LINK_DISCOVERING,
    HOST_LINK_IDENTITY,
    HOST_LINK_CAPABILITIES,
    HOST_LINK_ONLINE,
    HOST_LINK_DEGRADED
} HostLink_State;

typedef struct {
    HostLink_State state;
    uint32_t session_generation;
    uint32_t capabilities;
    uint32_t timeouts;
    uint32_t rejected_responses;
    char identity[33];
} HostNodeManager_Status;

void HostNodeManager_Initialize(void);
void HostNodeManager_Process(void);
HostNodeManager_Status HostNodeManager_GetStatus(void);
const char *HostNodeManager_StateName(HostLink_State state);

#endif
