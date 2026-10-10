#include "diagnostics/Diagnostics.h"

void Diagnostics_Initialize(Diagnostics_State *state) {
    if (state != 0) {
        state->fault_count = 0U;
    }
}

void Diagnostics_RecordFault(Diagnostics_State *state) {
    if (state != 0 && state->fault_count != UINT32_MAX) {
        state->fault_count++;
    }
}
