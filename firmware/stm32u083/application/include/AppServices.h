#ifndef APP_SERVICES_H
#define APP_SERVICES_H

#include <stdbool.h>
#include <stdint.h>

#include "AppDiagnostics.h"
#include "node_api/NodeServices.h"
#include "virtual_io/VirtualIo.h"

typedef struct {
    NodeApi_Registry registry;
    AppDiagnostics_State diagnostics;
    VirtualIo_Instance virtual_io;
    uint32_t button_event_counter;
} AppServices_State;

NodeApi_Status AppServices_Initialize(AppServices_State *state, uint32_t seed);
NodeApi_Status AppServices_Tick(AppServices_State *state, uint32_t now_ms);
NodeApi_Status AppServices_PublishButton(AppServices_State *state, bool pressed);
const char *AppServices_GetPersonalityName(void);

#endif
