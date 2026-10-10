#include "diagnostics/Diagnostics.h"

int main(void) {
    Diagnostics_State state;
    Diagnostics_Initialize(&state);
    Diagnostics_RecordFault(&state);
    return state.fault_count == 1U ? 0 : 1;
}
