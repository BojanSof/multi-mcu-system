#include "stm32u083_bootloader/BootTarget.h"
int main(void) {
    BootTarget_Initialize();
    BootTarget_ConsumeConfirmation();
    BootJournal_Flash journal = BootTarget_Journal();
    /* Open maintenance immediately after reset. This must run before cryptographic slot
     * validation: P-256 verification
     * on Cortex-M0+ can otherwise consume the complete host retry budget. */
    if (BootTarget_RecoveryRequested(2000U)) {
        BootJournal_Snapshot current;
        uint8_t active = BOOT_JOURNAL_SLOT_NONE;
        if (BootJournal_Load(&journal, &current) == BOOT_JOURNAL_OK) {
            if (current.record.state == BOOT_JOURNAL_STATE_CONFIRMED)
                active = current.record.confirmed_slot;
            else if (current.record.state == BOOT_JOURNAL_STATE_PENDING ||
                     current.record.state == BOOT_JOURNAL_STATE_TRIAL)
                active = current.record.selected_slot;
        }
        BootTarget_SetTrialMarker(false, 0U);
        BootTarget_RunRecovery(active);
    }
    const uint8_t valid_slots = BootTarget_ValidSlotMask();
    BootJournal_BootDecision decision;
    if (BootJournal_PrepareBoot(&journal, valid_slots, 2U, &decision) == BOOT_JOURNAL_OK &&
        !decision.recovery) {
        BootTarget_SetTrialMarker(decision.persisted_record.state == BOOT_JOURNAL_STATE_TRIAL,
                                  decision.slot);
        (void)BootTarget_Jump(decision.slot);
    }
    BootTarget_SetTrialMarker(false, 0U);
    BootTarget_RunRecovery(BOOT_JOURNAL_SLOT_NONE);
    for (;;) {
    }
}
