#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "AppFaultHistory.h"
#include "AppLifecycle.h"
#include "NodeTransport.h"
#include "stm32u083_platform/PlatformSideband.h"
#include "stm32u083_platform/PlatformSupervision.h"

static uint32_t Test_Cursor;
static unsigned int Test_ReadCalls;
static unsigned int Test_ClearCalls;
static bool Test_Ready;

Platform_ResetInfo PlatformSupervision_GetResetInfo(void) {
    const Platform_ResetInfo reset = {PLATFORM_RESET_WATCHDOG, 0x0010U, 5U};
    return reset;
}

void PlatformSideband_SetReady(bool ready) { Test_Ready = ready; }

NodeApi_Status AppFaultHistory_Read(uint32_t cursor, NodeApi_ServiceResponse *response) {
    Test_Cursor = cursor;
    ++Test_ReadCalls;
    (void)memset(response->data, 0, sizeof(response->data));
    response->data[0] = 1U;
    response->data[1] = 1U;
    response->data[12] = 17U;
    response->data_length = NODE_API_FAULT_HISTORY_RESPONSE_SIZE;
    return NODE_API_STATUS_OK;
}

NodeApi_Status AppFaultHistory_Clear(NodeApi_ServiceResponse *response) {
    ++Test_ClearCalls;
    (void)memset(response->data, 0, sizeof(response->data));
    response->data[1] = 1U;
    response->data_length = NODE_API_FAULT_HISTORY_HEADER_SIZE;
    return NODE_API_STATUS_OK;
}

void NodeTransport_GetEventStatus(NodeApi_ServiceResponse *response) {
    (void)memset(response->data, 0, sizeof(response->data));
    response->data[0] = 3U;
    response->data_length = 24U;
}

int main(void) {
    NodeApi_Registry registry;
    AppLifecycle_State lifecycle;
    NodeApi_ServiceResponse response;
    NodeApi_ServiceRequest request = {NODE_API_SERVICE_LIFECYCLE,
                                      NODE_API_LIFECYCLE_COMMAND_READ_FAULT_HISTORY, 1U, NULL, 0U};
    const uint8_t cursor[4] = {0x78U, 0x56U, 0x34U, 0x12U};
    NodeApi_RegistryInitialize(&registry);
    assert(AppLifecycle_Initialize(&lifecycle, &registry) == NODE_API_STATUS_OK);
    assert(NodeApi_GetService(&registry, 0U)->version_major == 1U);
    assert(NodeApi_GetService(&registry, 0U)->version_minor == 1U);
    assert(NodeApi_DispatchRequest(&registry, &request, &response) ==
           NODE_API_STATUS_INVALID_ARGUMENT);
    assert(Test_ReadCalls == 0U);
    request.data = cursor;
    request.data_length = 3U;
    assert(NodeApi_DispatchRequest(&registry, &request, &response) ==
           NODE_API_STATUS_INVALID_ARGUMENT);
    request.data_length = sizeof(cursor);
    assert(NodeApi_DispatchRequest(&registry, &request, &response) == NODE_API_STATUS_OK);
    assert(Test_Cursor == 0x12345678UL && Test_ReadCalls == 1U);
    assert(response.data_length == 60U && response.data[12] == 17U);
    /* An identical retry is cached and does not reread storage. */
    assert(NodeApi_DispatchRequest(&registry, &request, &response) == NODE_API_STATUS_OK);
    assert(Test_ReadCalls == 1U);

    request.command_id = NODE_API_LIFECYCLE_COMMAND_CLEAR_FAULT_HISTORY;
    request.operation_id = 2U;
    assert(NodeApi_DispatchRequest(&registry, &request, &response) ==
           NODE_API_STATUS_INVALID_ARGUMENT);
    assert(Test_ClearCalls == 0U);
    request.data = NULL;
    request.data_length = 0U;
    assert(NodeApi_DispatchRequest(&registry, &request, &response) == NODE_API_STATUS_OK);
    assert(response.data_length == 12U && response.data[1] == 1U);
    assert(NodeApi_DispatchRequest(&registry, &request, &response) == NODE_API_STATUS_OK);
    assert(Test_ClearCalls == 1U);

    request.command_id = NODE_API_LIFECYCLE_COMMAND_GET_EVENT_STATUS;
    request.operation_id = 3U;
    assert(NodeApi_DispatchRequest(&registry, &request, &response) == NODE_API_STATUS_OK);
    assert(response.data_length == 24U && response.data[0] == 3U);
    request.data = cursor;
    request.data_length = 1U;
    assert(NodeApi_DispatchRequest(&registry, &request, &response) ==
           NODE_API_STATUS_INVALID_ARGUMENT);

    request.command_id = NODE_API_LIFECYCLE_COMMAND_GET_STATUS;
    request.operation_id = 4U;
    request.data = NULL;
    request.data_length = 0U;
    assert(NodeApi_DispatchRequest(&registry, &request, &response) == NODE_API_STATUS_OK);
    assert(response.data_length == NODE_API_LIFECYCLE_STATUS_SIZE);
    assert(response.data[1] == NODE_API_RESET_REASON_WATCHDOG && response.data[4] == 5U);
    request.command_id = NODE_API_LIFECYCLE_COMMAND_ENTER_MAINTENANCE;
    request.operation_id = 5U;
    assert(NodeApi_DispatchRequest(&registry, &request, &response) == NODE_API_STATUS_OK);
    assert(AppLifecycle_IsQuiescent(&lifecycle));
    AppLifecycle_ApplyReadyState(&lifecycle);
    assert(!Test_Ready);
    AppLifecycle_ApplyReadyState(&lifecycle);
    assert(Test_Ready);
    (void)puts("STM lifecycle reliability command validation: PASS");
    return 0;
}
