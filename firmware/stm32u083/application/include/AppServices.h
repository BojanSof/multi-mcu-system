#ifndef APP_SERVICES_H
#define APP_SERVICES_H

#include <stdbool.h>
#include <stdint.h>

#include "AppDiagnostics.h"
#include "AppLifecycle.h"
#include "node_api/NodeServices.h"
#include "virtual_io/VirtualIo.h"

typedef struct {
    NodeApi_Registry registry;
    AppDiagnostics_State diagnostics;
    AppLifecycle_State lifecycle;
    VirtualIo_Instance virtual_io;
    uint32_t button_event_counter;
} AppServices_State;

NodeApi_Status AppServices_Initialize(AppServices_State *state, uint32_t seed);
NodeApi_Status AppServices_Tick(AppServices_State *state, uint32_t now_ms);
NodeApi_Status AppServices_PublishButton(AppServices_State *state, bool pressed);
void AppServices_RecordProgress(AppServices_State *state);
uint32_t AppServices_GetProgressCount(const AppServices_State *state);
bool AppServices_IsMaintenance(const AppServices_State *state);
bool AppServices_IsMaintenanceActive(void);
bool AppServices_IsQuiescentActive(void);
void AppServices_MarkReady(AppServices_State *state);
void AppServices_MarkOnline(void);
void AppServices_ApplyReadyState(AppServices_State *state);
const char *AppServices_GetPersonalityName(void);

#endif
