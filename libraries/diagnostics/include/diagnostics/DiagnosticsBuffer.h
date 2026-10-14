#ifndef DIAGNOSTICS_DIAGNOSTICS_BUFFER_H
#define DIAGNOSTICS_DIAGNOSTICS_BUFFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "diagnostics/DiagnosticsFault.h"

#define DIAGNOSTICS_RECORD_CAPACITY 16U

typedef struct {
    Diagnostics_Fault records[DIAGNOSTICS_RECORD_CAPACITY];
    size_t head;
    size_t count;
    uint32_t overflow_count;
    uint32_t total_recorded;
    uint32_t injection_mask;
} Diagnostics_State;

void Diagnostics_Initialize(Diagnostics_State *state);
bool Diagnostics_RecordFault(Diagnostics_State *state, const Diagnostics_Fault *fault);
bool Diagnostics_PopFault(Diagnostics_State *state, Diagnostics_Fault *fault);
size_t Diagnostics_GetCount(const Diagnostics_State *state);
bool Diagnostics_EnableInjection(Diagnostics_State *state, Diagnostics_InjectionId injection);
void Diagnostics_DisableAllInjections(Diagnostics_State *state);
bool Diagnostics_IsInjectionEnabled(const Diagnostics_State *state,
                                    Diagnostics_InjectionId injection);

#endif
