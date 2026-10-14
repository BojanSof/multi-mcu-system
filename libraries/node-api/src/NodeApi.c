#include "node_api/NodeApi.h"
#include "node_api/NodeServices.h"

#include <string.h>

NodeApi_Version NodeApi_GetVersion(void) {
    const NodeApi_Version version = {NODE_API_VERSION_MAJOR, NODE_API_VERSION_MINOR};
    return version;
}

void NodeApi_RegistryInitialize(NodeApi_Registry *registry) {
    if (registry != NULL) {
        (void)memset(registry, 0, sizeof(*registry));
    }
}

NodeApi_Status NodeApi_RegisterService(NodeApi_Registry *registry,
                                       const NodeApi_ServiceRegistration *registration) {
    size_t index;

    if ((registry == NULL) || (registration == NULL) || (registration->handler == NULL) ||
        (registration->descriptor.service_id == 0U)) {
        return NODE_API_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0U; index < registry->service_count; ++index) {
        if (registry->services[index].descriptor.service_id ==
            registration->descriptor.service_id) {
            return NODE_API_STATUS_INVALID_ARGUMENT;
        }
    }
    if (registry->service_count >= NODE_API_MAX_SERVICES) {
        return NODE_API_STATUS_CAPACITY_EXCEEDED;
    }
    registry->services[registry->service_count] = *registration;
    ++registry->service_count;
    return NODE_API_STATUS_OK;
}

size_t NodeApi_GetServiceCount(const NodeApi_Registry *registry) {
    return (registry != NULL) ? registry->service_count : 0U;
}

const NodeApi_ServiceDescriptor *NodeApi_GetService(const NodeApi_Registry *registry,
                                                    size_t index) {
    if ((registry == NULL) || (index >= registry->service_count)) {
        return NULL;
    }
    return &registry->services[index].descriptor;
}

NodeApi_Status NodeApi_DispatchRequest(NodeApi_Registry *registry,
                                       const NodeApi_ServiceRequest *request,
                                       NodeApi_ServiceResponse *response) {
    size_t index;

    if (response != NULL) {
        (void)memset(response, 0, sizeof(*response));
        response->status = NODE_API_STATUS_INVALID_ARGUMENT;
    }
    if ((registry == NULL) || (request == NULL) || (response == NULL) ||
        ((request->data_length != 0U) && (request->data == NULL)) ||
        (request->data_length > NODE_API_SERVICE_DATA_MAX_LENGTH)) {
        return NODE_API_STATUS_INVALID_ARGUMENT;
    }
    if (registry->replay_valid && (request->service_id == registry->replay_service_id) &&
        (request->command_id == registry->replay_command_id) &&
        (request->operation_id == registry->replay_operation_id) &&
        (request->data_length == registry->replay_request_data_length) &&
        ((request->data_length == 0U) ||
         (memcmp(request->data, registry->replay_request_data, request->data_length) == 0))) {
        *response = registry->replay_response;
        return response->status;
    }
    for (index = 0U; index < registry->service_count; ++index) {
        if (registry->services[index].descriptor.service_id == request->service_id) {
            const NodeApi_Status status = registry->services[index].handler(
                registry->services[index].context, request, response);
            if (response->data_length > NODE_API_SERVICE_DATA_MAX_LENGTH) {
                response->data_length = 0U;
                response->status = NODE_API_STATUS_CAPACITY_EXCEEDED;
                return NODE_API_STATUS_CAPACITY_EXCEEDED;
            }
            response->status = status;
            if (status == NODE_API_STATUS_OK) {
                registry->replay_service_id = request->service_id;
                registry->replay_command_id = request->command_id;
                registry->replay_operation_id = request->operation_id;
                registry->replay_request_data_length = request->data_length;
                if (request->data_length != 0U) {
                    (void)memcpy(registry->replay_request_data, request->data,
                                 request->data_length);
                }
                registry->replay_response = *response;
                registry->replay_valid = true;
            }
            return status;
        }
    }
    response->status = NODE_API_STATUS_UNSUPPORTED_SERVICE;
    return NODE_API_STATUS_UNSUPPORTED_SERVICE;
}
