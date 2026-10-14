#include "diagnostics/Diagnostics.h"

#include <limits.h>
#include <string.h>

static bool Diagnostics_IsFaultValid(const Diagnostics_Fault *fault) {
    return (fault != NULL) && (fault->source >= (uint8_t)DIAGNOSTICS_SOURCE_HOST) &&
           (fault->source <= (uint8_t)DIAGNOSTICS_SOURCE_TOOL) &&
           (fault->severity >= (uint8_t)DIAGNOSTICS_SEVERITY_INFO) &&
           (fault->severity <= (uint8_t)DIAGNOSTICS_SEVERITY_FATAL) &&
           (fault->domain >= (uint8_t)DIAGNOSTICS_DOMAIN_PROTOCOL) &&
           (fault->domain <= (uint8_t)DIAGNOSTICS_DOMAIN_RUNTIME);
}

static bool Diagnostics_IsInjectionValid(Diagnostics_InjectionId injection) {
    return ((unsigned int)injection >= (unsigned int)DIAGNOSTICS_INJECTION_EARLY_READY) &&
           ((unsigned int)injection <= (unsigned int)DIAGNOSTICS_INJECTION_MAX);
}

static uint32_t Diagnostics_InjectionBit(Diagnostics_InjectionId injection) {
    return UINT32_C(1) << ((unsigned int)injection - 1U);
}

static void Diagnostics_IncrementSaturating(uint32_t *value) {
    if (*value != UINT32_MAX) {
        ++(*value);
    }
}

void Diagnostics_Initialize(Diagnostics_State *state) {
    if (state != NULL) {
        (void)memset(state, 0, sizeof(*state));
    }
}

bool Diagnostics_RecordFault(Diagnostics_State *state, const Diagnostics_Fault *fault) {
    size_t write_index;

    if ((state == NULL) || !Diagnostics_IsFaultValid(fault)) {
        return false;
    }
    if (state->count == DIAGNOSTICS_RECORD_CAPACITY) {
        write_index = state->head;
        state->head = (state->head + 1U) % DIAGNOSTICS_RECORD_CAPACITY;
        Diagnostics_IncrementSaturating(&state->overflow_count);
    } else {
        write_index = (state->head + state->count) % DIAGNOSTICS_RECORD_CAPACITY;
        ++state->count;
    }
    state->records[write_index] = *fault;
    Diagnostics_IncrementSaturating(&state->total_recorded);
    return true;
}

bool Diagnostics_PopFault(Diagnostics_State *state, Diagnostics_Fault *fault) {
    if ((state == NULL) || (fault == NULL) || (state->count == 0U)) {
        return false;
    }
    *fault = state->records[state->head];
    state->head = (state->head + 1U) % DIAGNOSTICS_RECORD_CAPACITY;
    --state->count;
    if (state->count == 0U) {
        state->head = 0U;
    }
    return true;
}

size_t Diagnostics_GetCount(const Diagnostics_State *state) {
    return (state != NULL) ? state->count : 0U;
}

bool Diagnostics_EnableInjection(Diagnostics_State *state, Diagnostics_InjectionId injection) {
    if ((state == NULL) || !Diagnostics_IsInjectionValid(injection)) {
        return false;
    }
    state->injection_mask |= Diagnostics_InjectionBit(injection);
    return true;
}

void Diagnostics_DisableAllInjections(Diagnostics_State *state) {
    if (state != NULL) {
        state->injection_mask = 0U;
    }
}

bool Diagnostics_IsInjectionEnabled(const Diagnostics_State *state,
                                    Diagnostics_InjectionId injection) {
    return (state != NULL) && Diagnostics_IsInjectionValid(injection) &&
           ((state->injection_mask & Diagnostics_InjectionBit(injection)) != 0U);
}
