#ifndef APP_LIFECYCLE_H
#define APP_LIFECYCLE_H

#include <stdbool.h>
#include <stdint.h>

#include "node_api/NodeServices.h"

typedef struct {
    NodeApi_LifecycleState state;
    NodeApi_ResetReason reset_reason;
    uint16_t raw_reset_flags;
    uint32_t boot_count;
    volatile uint32_t progress_count;
    uint8_t ready_transition_polls;
} AppLifecycle_State;

NodeApi_Status AppLifecycle_Initialize(AppLifecycle_State *state, NodeApi_Registry *registry);
void AppLifecycle_RecordProgress(AppLifecycle_State *state);
bool AppLifecycle_IsMaintenance(const AppLifecycle_State *state);
bool AppLifecycle_IsQuiescent(const AppLifecycle_State *state);
void AppLifecycle_MarkReady(AppLifecycle_State *state);
void AppLifecycle_MarkOnline(AppLifecycle_State *state);
void AppLifecycle_ApplyReadyState(AppLifecycle_State *state);

#endif
