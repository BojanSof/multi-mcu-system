#ifndef DIAGNOSTICS_DIAGNOSTICS_H
#define DIAGNOSTICS_DIAGNOSTICS_H

#include <stdint.h>

typedef struct {
    uint32_t fault_count;
} Diagnostics_State;

void Diagnostics_Initialize(Diagnostics_State *state);
void Diagnostics_RecordFault(Diagnostics_State *state);

#endif
