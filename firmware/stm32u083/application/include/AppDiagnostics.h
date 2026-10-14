#ifndef APP_DIAGNOSTICS_H
#define APP_DIAGNOSTICS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "diagnostics/Diagnostics.h"
#include "node_api/NodeServices.h"
#include "virtual_io/VirtualIo.h"

typedef struct {
    Diagnostics_State diagnostics;
    bool drop_acceptance_pending;
    bool task_stall_pending;
    bool corrupt_pending;
    bool truncate_pending;
    bool record_event_pending;
    uint8_t debug_marker_hold_cycles;
    uint32_t last_output_operation_id;
    VirtualIo_Instance *virtual_io;
    Diagnostics_Fault pending_record_event;
} AppDiagnostics_State;

NodeApi_Status AppDiagnostics_Initialize(AppDiagnostics_State *state, NodeApi_Registry *registry,
                                         VirtualIo_Instance *virtual_io);
bool AppDiagnostics_IsInjectionEnabled(Diagnostics_InjectionId injection);
bool AppDiagnostics_ShouldDropAcceptance(uint16_t service_id, uint16_t command_id,
                                         uint32_t operation_id);
void AppDiagnostics_PrepareOutgoing(uint8_t *encoded, size_t *encoded_length);
void AppDiagnostics_DelayResponse(void);
bool AppDiagnostics_ConsumeTaskStall(void);
void AppDiagnostics_RecordOutputOrdering(uint32_t operation_id);
void AppDiagnostics_ProcessEvents(void);

#endif
