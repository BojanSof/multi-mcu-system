#ifndef HOST_SIGNALS_H
#define HOST_SIGNALS_H

#include <stdbool.h>

int HostSignals_Initialize(void);
bool HostSignals_NodeReady(void);
bool HostSignals_NodeIrqAsserted(void);
int HostSignals_SetDebug(bool active);

#endif
