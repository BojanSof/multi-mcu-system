#ifndef HOST_RELIABILITY_H
#define HOST_RELIABILITY_H
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    const char *health;
    const char *reason;
    uint32_t recovery_attempts;
    uint32_t recovery_total;
    uint32_t reset_cause;
    bool watchdog;
    bool gpio_interrupts;
    bool failed;
} HostReliability_Status;
int HostReliability_Initialize(void);
void HostReliability_Process(void);
void HostReliability_MainProgress(void);
void HostReliability_Clear(void);
HostReliability_Status HostReliability_GetStatus(void);
#endif
