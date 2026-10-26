#ifndef APP_FAULT_HISTORY_H
#define APP_FAULT_HISTORY_H

#include <stdint.h>

#include "diagnostics/DiagnosticsFault.h"
#include "node_api/NodeServices.h"

/* Main/application-task ownership only. Optional profile returns unsupported
 * without touching flash when MULTI_MCU_RELIABILITY is not enabled. */
void AppFaultHistory_Initialize(uint32_t session);
void AppFaultHistory_RecordFault(const Diagnostics_Fault *fault);
NodeApi_Status AppFaultHistory_Read(uint32_t cursor, NodeApi_ServiceResponse *response);
NodeApi_Status AppFaultHistory_Clear(NodeApi_ServiceResponse *response);

#endif
