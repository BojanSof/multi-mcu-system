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
    HOST_LINK_DEGRADED,
    HOST_LINK_FAULT_LATCHED
} HostLink_State;

typedef struct {
    HostLink_State state;
    uint32_t session_generation;
    uint32_t capabilities;
    uint32_t timeouts;
    uint32_t ready_losses;
    uint32_t session_changes;
    uint32_t node_reboots;
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
    NodeApi_LifecycleState node_lifecycle_state;
    NodeApi_ResetReason node_reset_reason;
    uint16_t node_raw_reset_flags;
    uint32_t node_boot_count;
    uint32_t node_progress_count;
    uint16_t node_firmware_major;
    uint16_t node_firmware_minor;
    uint16_t node_firmware_patch;
    uint8_t node_wire_major;
    uint8_t node_wire_minor;
    uint16_t node_management_major;
    uint16_t node_management_minor;
    uint32_t node_build_id;
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
int HostNodeManager_LifecycleGetStatus(void);
int HostNodeManager_LifecycleGetVersions(void);
int HostNodeManager_LifecycleEnterMaintenance(void);
int HostNodeManager_LifecycleExitMaintenance(void);
int HostNodeManager_LifecycleEnterShutdown(void);
int HostNodeManager_LifecycleExitShutdown(void);

#endif
