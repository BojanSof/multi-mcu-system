#ifndef HOST_NODE_MANAGER_H
#define HOST_NODE_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "node_api/NodeCommands.h"
#include "node_api/NodeServices.h"

typedef enum {
    HOST_LINK_DISABLED = 0,
    HOST_LINK_DISCOVERING,
    HOST_LINK_IDENTITY,
    HOST_LINK_CAPABILITIES,
    HOST_LINK_SERVICES,
    HOST_LINK_ONLINE,
    HOST_LINK_DEGRADED
} HostLink_State;

typedef struct {
    HostLink_State state;
    uint32_t session_generation;
    uint32_t capabilities;
    uint32_t timeouts;
    uint32_t rejected_responses;
    uint32_t received_events;
    uint32_t diagnostic_records;
    uint32_t diagnostic_overflows;
    uint32_t node_injection_mask;
    uint8_t node_diagnostic_queued;
    uint32_t next_operation_id;
    size_t service_count;
    NodeApi_ServiceDescriptor services[NODE_API_MAX_SERVICES];
    char identity[33];
} HostNodeManager_Status;

void HostNodeManager_Initialize(void);
void HostNodeManager_Process(void);
HostNodeManager_Status HostNodeManager_GetStatus(void);
const char *HostNodeManager_StateName(HostLink_State state);
bool HostNodeManager_HasService(uint16_t service_id);
int HostNodeManager_CallService(uint16_t service_id, uint16_t command_id, const uint8_t *data,
                                size_t data_length, uint32_t *operation_id);
int HostNodeManager_DiagnosticsGetStatus(void);
int HostNodeManager_DiagnosticsEnable(uint8_t injection_id);
int HostNodeManager_DiagnosticsDisableAll(void);
int HostNodeManager_DiagnosticsGetRecord(void);

#endif
