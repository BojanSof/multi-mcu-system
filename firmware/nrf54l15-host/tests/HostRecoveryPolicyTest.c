#include "HostRecoveryPolicy.h"
#include <assert.h>
#include <string.h>
int main(void) {
    HostRecoveryPolicy_State s;
    HostRecoveryPolicy_Initialize(&s, 0U);
    assert(!HostRecoveryPolicy_Process(&s, 4999, false, false, false));
    assert(HostRecoveryPolicy_Process(&s, 5000, false, false, false));
    assert(s.attempts == 1 && s.total == 1);
    assert(!HostRecoveryPolicy_Process(&s, 15000, false, false, false));
    assert(!HostRecoveryPolicy_Process(&s, 15999, false, false, false));
    assert(HostRecoveryPolicy_Process(&s, 16000, false, false, false));
    assert(!HostRecoveryPolicy_Process(&s, 26000, false, false, false));
    assert(HostRecoveryPolicy_Process(&s, 31000, false, false, false));
    assert(!HostRecoveryPolicy_Process(&s, 41000, false, false, false));
    assert(s.latched && s.total == 3 && strcmp(s.health, "failed") == 0);
    assert(!HostRecoveryPolicy_Process(&s, 80000, true, false, false));
    assert(s.latched);
    HostRecoveryPolicy_Clear(&s, 80000);
    assert(!s.latched && s.total == 3);
    assert(!HostRecoveryPolicy_Process(&s, 90000, false, true, false));
    assert(s.attempts == 0);
    assert(!HostRecoveryPolicy_Process(&s, 94999, false, false, false));
    assert(!HostRecoveryPolicy_Process(&s, 95000, true, false, false));
    assert(strcmp(s.health, "healthy") == 0);
    assert(!HostRecoveryPolicy_Process(&s, 100000, false, false, false));
    assert(!HostRecoveryPolicy_Process(&s, 104999, false, false, false));
    assert(HostRecoveryPolicy_Process(&s, 105000, false, false, false));
    assert(!HostRecoveryPolicy_Process(&s, 105010, false, false, true));
    assert(s.latched && strcmp(s.reason, "repeated_reboot") == 0);
    /* Recovered failures near the old fixed-window boundary still share one
     * rolling 60-second budget. */
    HostRecoveryPolicy_Initialize(&s, 0U);
    assert(!HostRecoveryPolicy_Process(&s, 50000, true, false, false));
    assert(!HostRecoveryPolicy_Process(&s, 50000, false, false, false));
    assert(HostRecoveryPolicy_Process(&s, 55000, false, false, false));
    assert(!HostRecoveryPolicy_Process(&s, 56000, true, false, false));
    assert(!HostRecoveryPolicy_Process(&s, 61000, false, false, false));
    assert(HostRecoveryPolicy_Process(&s, 66000, false, false, false));
    assert(!HostRecoveryPolicy_Process(&s, 67000, true, false, false));
    assert(!HostRecoveryPolicy_Process(&s, 76000, false, false, false));
    assert(HostRecoveryPolicy_Process(&s, 81000, false, false, false));
    assert(!HostRecoveryPolicy_Process(&s, 82000, true, false, false));
    assert(!HostRecoveryPolicy_Process(&s, 83000, false, false, false));
    assert(!HostRecoveryPolicy_Process(&s, 88000, false, false, false));
    assert(s.latched && s.total == 3U);
    return 0;
}
