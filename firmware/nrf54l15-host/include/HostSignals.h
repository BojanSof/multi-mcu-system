#ifndef HOST_SIGNALS_H
#define HOST_SIGNALS_H

#include <stdbool.h>

int HostSignals_Initialize(void);
/* True only when both optional sideband interrupt callbacks were enabled. */
bool HostSignals_InterruptsEnabled(void);
bool HostSignals_NodeReady(void);
bool HostSignals_NodeIrqAsserted(void);
int HostSignals_SetDebug(bool active);
/* ISR-safe wake and main-thread bounded wait for the reliability profile. */
void HostSignals_Wake(void);
void HostSignals_Wait(void);

#endif
