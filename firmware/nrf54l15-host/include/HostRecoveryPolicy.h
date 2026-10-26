#ifndef HOST_RECOVERY_POLICY_H
#define HOST_RECOVERY_POLICY_H
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    uint64_t deadline_ms;
    uint64_t pulse_ms[3];
    uint32_t consecutive;
    uint32_t attempts;
    uint32_t total;
    bool waiting;
    bool latched;
    bool online;
    const char *health;
    const char *reason;
} HostRecoveryPolicy_State;
void HostRecoveryPolicy_Initialize(HostRecoveryPolicy_State *state, uint64_t now);
void HostRecoveryPolicy_Clear(HostRecoveryPolicy_State *state, uint64_t now);
bool HostRecoveryPolicy_Process(HostRecoveryPolicy_State *state, uint64_t now, bool online,
                                bool blocked, bool peer_latched);
#endif
