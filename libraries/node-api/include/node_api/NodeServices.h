#ifndef NODE_API_NODE_SERVICES_H
#define NODE_API_NODE_SERVICES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "node_api/NodeCommands.h"

#define NODE_API_SERVICE_GPIO 0x0001U
#define NODE_API_SERVICE_DIAGNOSTICS 0x0002U

#define NODE_API_GPIO_EVENT_BUTTON_CHANGED 0x0001U
#define NODE_API_GPIO_BUTTON_EVENT_DATA_SIZE 1U

#define NODE_API_DIAGNOSTICS_COMMAND_GET_STATUS 0x0001U
#define NODE_API_DIAGNOSTICS_COMMAND_ENABLE_INJECTION 0x0002U
#define NODE_API_DIAGNOSTICS_COMMAND_DISABLE_ALL 0x0003U
#define NODE_API_DIAGNOSTICS_COMMAND_GET_RECORD 0x0004U
#define NODE_API_DIAGNOSTICS_EVENT_RECORD 0x0001U

typedef struct {
    uint16_t service_id;
    uint16_t version_major;
    uint16_t version_minor;
    uint16_t flags;
} NodeApi_ServiceDescriptor;

typedef struct {
    uint16_t service_id;
    uint16_t command_id;
    uint32_t operation_id;
    const uint8_t *data;
    size_t data_length;
} NodeApi_ServiceRequest;

typedef struct {
    NodeApi_Status status;
    uint8_t data[NODE_API_SERVICE_DATA_MAX_LENGTH];
    size_t data_length;
} NodeApi_ServiceResponse;

typedef NodeApi_Status (*NodeApi_ServiceHandler)(void *context,
                                                 const NodeApi_ServiceRequest *request,
                                                 NodeApi_ServiceResponse *response);

typedef struct {
    NodeApi_ServiceDescriptor descriptor;
    NodeApi_ServiceHandler handler;
    void *context;
} NodeApi_ServiceRegistration;

typedef struct {
    NodeApi_ServiceRegistration services[NODE_API_MAX_SERVICES];
    size_t service_count;
    bool replay_valid;
    uint16_t replay_service_id;
    uint16_t replay_command_id;
    uint32_t replay_operation_id;
    uint8_t replay_request_data[NODE_API_SERVICE_DATA_MAX_LENGTH];
    size_t replay_request_data_length;
    NodeApi_ServiceResponse replay_response;
} NodeApi_Registry;

void NodeApi_RegistryInitialize(NodeApi_Registry *registry);
NodeApi_Status NodeApi_RegisterService(NodeApi_Registry *registry,
                                       const NodeApi_ServiceRegistration *registration);
size_t NodeApi_GetServiceCount(const NodeApi_Registry *registry);
const NodeApi_ServiceDescriptor *NodeApi_GetService(const NodeApi_Registry *registry, size_t index);
NodeApi_Status NodeApi_DispatchRequest(NodeApi_Registry *registry,
                                       const NodeApi_ServiceRequest *request,
                                       NodeApi_ServiceResponse *response);

#endif
