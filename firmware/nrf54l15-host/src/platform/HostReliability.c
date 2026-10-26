#include "HostReliability.h"
#include "HostNodeManager.h"
#include "HostRecoveryPolicy.h"
#include "HostSignals.h"
#include "HostUpdateAdapter.h"
#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

static HostRecoveryPolicy_State HostReliability_Policy;
static uint32_t HostReliability_ResetCause;
static const struct device *const HostReliability_Wdt = DEVICE_DT_GET(DT_ALIAS(watchdog0));
static int HostReliability_Channel;
static bool HostReliability_Started;
static atomic_t HostReliability_WorkProgress;
static atomic_t HostReliability_MainCount;
static atomic_val_t HostReliability_LastMain;
static atomic_val_t HostReliability_LastWork;

static void HostReliability_Probe(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(HostReliability_Work, HostReliability_Probe);
static void HostReliability_Probe(struct k_work *work) {
    (void)work;
    atomic_inc(&HostReliability_WorkProgress);
    (void)k_work_reschedule(&HostReliability_Work, K_SECONDS(1));
}
int HostReliability_Initialize(void) {
    HostRecoveryPolicy_Initialize(&HostReliability_Policy, (uint64_t)k_uptime_get());
    (void)hwinfo_get_reset_cause(&HostReliability_ResetCause);
    (void)hwinfo_clear_reset_cause();
    if (!device_is_ready(HostReliability_Wdt)) {
        return -ENODEV;
    }
    const struct wdt_timeout_cfg timeout = {
        .window = {.min = 0, .max = 30000},
        .callback = NULL,
        .flags = WDT_FLAG_RESET_SOC,
    };
    HostReliability_Channel = wdt_install_timeout(HostReliability_Wdt, &timeout);
    if (HostReliability_Channel < 0) {
        return HostReliability_Channel;
    }
    int result = wdt_setup(HostReliability_Wdt, WDT_OPT_PAUSE_HALTED_BY_DBG);
    if (result != 0) {
        return result;
    }
    HostReliability_Started = true;
    (void)k_work_reschedule(&HostReliability_Work, K_NO_WAIT);
    return 0;
}
void HostReliability_MainProgress(void) {
    atomic_inc(&HostReliability_MainCount);
    const atomic_val_t main = atomic_get(&HostReliability_MainCount);
    const atomic_val_t work = atomic_get(&HostReliability_WorkProgress);
    if (HostReliability_Started && main != HostReliability_LastMain &&
        work != HostReliability_LastWork) {
        if (wdt_feed(HostReliability_Wdt, HostReliability_Channel) == 0) {
            HostReliability_LastMain = main;
            HostReliability_LastWork = work;
        }
    }
}
void HostReliability_Process(void) {
    const HostNodeManager_Status status = HostNodeManager_GetStatus();
    const bool blocked = HostUpdateAdapter_IsBusy() || HostNodeManager_RecoverySuppressed() ||
                         status.node_lifecycle_state == NODE_API_LIFECYCLE_MAINTENANCE ||
                         status.node_lifecycle_state == NODE_API_LIFECYCLE_SHUTDOWN ||
                         status.state == HOST_LINK_DISABLED;
    if (HostRecoveryPolicy_Process(&HostReliability_Policy, (uint64_t)k_uptime_get(),
                                   status.state == HOST_LINK_ONLINE, blocked,
                                   status.state == HOST_LINK_FAULT_LATCHED)) {
        int result = HostUpdateAdapter_TryRecoveryReset();
        if (result == 0) {
            HostNodeManager_Initialize();
        } else if (result == -EBUSY) {
            /* Ownership changed after the snapshot. Do not consume a reset budget. */
            --HostReliability_Policy.attempts;
            --HostReliability_Policy.consecutive;
            if (HostReliability_Policy.total != UINT32_MAX) {
                --HostReliability_Policy.total;
            }
            HostReliability_Policy.waiting = false;
            HostReliability_Policy.deadline_ms = (uint64_t)k_uptime_get() + 5000U;
        } else {
            HostReliability_Policy.latched = true;
            HostReliability_Policy.health = "failed";
            HostReliability_Policy.reason = "reset_unavailable";
        }
    }
    if (HostReliability_Policy.latched && status.state != HOST_LINK_FAULT_LATCHED) {
        HostNodeManager_LatchFault();
    }
}
void HostReliability_Clear(void) {
    HostRecoveryPolicy_Clear(&HostReliability_Policy, (uint64_t)k_uptime_get());
    HostNodeManager_Initialize();
}
HostReliability_Status HostReliability_GetStatus(void) {
    return (HostReliability_Status){.health = HostReliability_Policy.health,
                                    .reason = HostReliability_Policy.reason,
                                    .recovery_attempts = HostReliability_Policy.attempts,
                                    .recovery_total = HostReliability_Policy.total,
                                    .reset_cause = HostReliability_ResetCause,
                                    .watchdog = HostReliability_Started,
                                    .gpio_interrupts = HostSignals_InterruptsEnabled(),
                                    .failed = HostReliability_Policy.latched};
}
