#include "node_api/NodeApi.h"
#include "node_api/NodeServices.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            (void)fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #condition);    \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static NodeApi_Status Test_Handler(void *context, const NodeApi_ServiceRequest *request,
                                   NodeApi_ServiceResponse *response) {
    uint32_t *calls = (uint32_t *)context;
    ++(*calls);
    if (request->command_id != 7U) {
        return NODE_API_STATUS_UNSUPPORTED_COMMAND;
    }
    response->data[0] = 0x5AU;
    response->data_length = 1U;
    return NODE_API_STATUS_OK;
}

int main(void) {
    const NodeApi_Version version = NodeApi_GetVersion();
    NodeApi_Registry registry;
    NodeApi_ServiceResponse response;
    uint32_t calls = 0U;
    const NodeApi_ServiceRegistration registration = {{0x2222U, 1U, 0U, 0U}, Test_Handler, &calls};
    const NodeApi_ServiceRequest valid = {0x2222U, 7U, 9U, NULL, 0U};
    const NodeApi_ServiceRequest bad_command = {0x2222U, 8U, 9U, NULL, 0U};
    const NodeApi_ServiceRequest missing = {0x3333U, 7U, 9U, NULL, 0U};

    CHECK(version.major == 1U && version.minor == 0U);
    NodeApi_RegistryInitialize(&registry);
    CHECK(NodeApi_GetServiceCount(&registry) == 0U);
    CHECK(NodeApi_RegisterService(&registry, &registration) == NODE_API_STATUS_OK);
    CHECK(NodeApi_RegisterService(&registry, &registration) == NODE_API_STATUS_INVALID_ARGUMENT);
    CHECK(NodeApi_GetServiceCount(&registry) == 1U);
    CHECK(NodeApi_GetService(&registry, 0U)->service_id == 0x2222U);
    CHECK(NodeApi_GetService(&registry, 1U) == NULL);
    CHECK(NodeApi_DispatchRequest(&registry, &valid, &response) == NODE_API_STATUS_OK);
    CHECK(response.status == NODE_API_STATUS_OK && response.data_length == 1U &&
          response.data[0] == 0x5AU && calls == 1U);
    CHECK(NodeApi_DispatchRequest(&registry, &bad_command, &response) ==
          NODE_API_STATUS_UNSUPPORTED_COMMAND);
    CHECK(response.data_length == 0U && calls == 2U);
    CHECK(NodeApi_DispatchRequest(&registry, &missing, &response) ==
          NODE_API_STATUS_UNSUPPORTED_SERVICE);
    CHECK(response.status == NODE_API_STATUS_UNSUPPORTED_SERVICE && calls == 2U);

    NodeApi_RegistryInitialize(&registry);
    for (size_t index = 0U; index < NODE_API_MAX_SERVICES; ++index) {
        NodeApi_ServiceRegistration item = registration;
        item.descriptor.service_id = (uint16_t)(0x4000U + index);
        CHECK(NodeApi_RegisterService(&registry, &item) == NODE_API_STATUS_OK);
    }
    {
        NodeApi_ServiceRegistration overflow = registration;
        overflow.descriptor.service_id = 0x5000U;
        CHECK(NodeApi_RegisterService(&registry, &overflow) == NODE_API_STATUS_CAPACITY_EXCEEDED);
    }
    return 0;
}
