#include "HostRecoveryPolicy.h"
#include <string.h>
void HostRecoveryPolicy_Initialize(HostRecoveryPolicy_State *state, uint64_t now) {
    memset(state, 0, sizeof(*state));
    state->deadline_ms = now + 5000U;
    state->health = "degraded";
    state->reason = "startup";
}
void HostRecoveryPolicy_Clear(HostRecoveryPolicy_State *state, uint64_t now) {
    const uint32_t total = state->total;
    HostRecoveryPolicy_Initialize(state, now);
    state->total = total;
    state->reason = "operator_clear";
}
bool HostRecoveryPolicy_Process(HostRecoveryPolicy_State *state, uint64_t now, bool online,
                                bool blocked, bool peer_latched) {
    if (blocked && !state->latched) {
        state->deadline_ms = now + 5000U;
        state->waiting = false;
        state->online = online;
        state->health = online ? "healthy" : "degraded";
        state->reason = "intentional_transition";
        return false;
    }
    if (peer_latched) {
        state->latched = true;
        state->reason = "repeated_reboot";
    }
    if (state->latched) {
        state->health = "failed";
        return false;
    }
    uint32_t kept = 0U;
    for (uint32_t i = 0U; i < state->attempts; ++i) {
        if (now - state->pulse_ms[i] < 60000U) {
            state->pulse_ms[kept++] = state->pulse_ms[i];
        }
    }
    state->attempts = kept;
    if (online) {
        state->online = true;
        state->consecutive = 0U;
        state->waiting = false;
        state->deadline_ms = now + 5000U;
        state->health = "healthy";
        state->reason = "online";
        return false;
    }
    if (state->online) {
        state->online = false;
        state->deadline_ms = now + 5000U;
    }
    state->health = state->waiting ? "recovering" : "degraded";
    state->reason = state->waiting ? "awaiting_discovery" : "discovery";
    if (now < state->deadline_ms) {
        return false;
    }
    if (state->waiting) {
        state->waiting = false;
        if (state->consecutive >= 3U) {
            state->latched = true;
            state->health = "failed";
            state->reason = "recovery_exhausted";
            return false;
        }
        state->deadline_ms = now + (state->consecutive == 1U ? 1000U : 5000U);
        state->reason = "backoff";
        return false;
    }
    if (state->attempts >= 3U) {
        state->latched = true;
        state->health = "failed";
        state->reason = "reset_limit";
        return false;
    }
    state->pulse_ms[state->attempts++] = now;
    ++state->consecutive;
    if (state->total != UINT32_MAX) {
        ++state->total;
    }
    state->waiting = true;
    state->deadline_ms = now + 10000U;
    state->health = "recovering";
    state->reason = "reset";
    return true;
}
