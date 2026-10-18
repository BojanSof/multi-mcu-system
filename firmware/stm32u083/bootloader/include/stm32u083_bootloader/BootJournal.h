#ifndef STM32U083_BOOTLOADER_BOOT_JOURNAL_H
#define STM32U083_BOOTLOADER_BOOT_JOURNAL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define BOOT_JOURNAL_PAGE_SIZE 2048U
#define BOOT_JOURNAL_PAGE_COUNT 2U
#define BOOT_JOURNAL_RECORD_SIZE 64U
#define BOOT_JOURNAL_RECORDS_PER_PAGE (BOOT_JOURNAL_PAGE_SIZE / BOOT_JOURNAL_RECORD_SIZE)
#define BOOT_JOURNAL_SLOT_NONE 0xFFU
typedef enum {
    BOOT_JOURNAL_STATE_CONFIRMED = 1,
    BOOT_JOURNAL_STATE_PENDING = 2,
    BOOT_JOURNAL_STATE_TRIAL = 3,
    BOOT_JOURNAL_STATE_INVALID = 4,
    BOOT_JOURNAL_STATE_RECOVERY = 5
} BootJournal_State;
typedef enum {
    BOOT_JOURNAL_OK = 0,
    BOOT_JOURNAL_ERROR_ARGUMENT = -1,
    BOOT_JOURNAL_ERROR_IO = -2,
    BOOT_JOURNAL_ERROR_POWER_LOSS = -3,
    BOOT_JOURNAL_ERROR_NO_RECORD = -4
} BootJournal_Result;
typedef struct {
    uint32_t generation;
    BootJournal_State state;
    uint8_t confirmed_slot;
    uint8_t selected_slot;
    uint8_t trial_attempts;
} BootJournal_Record;
typedef struct {
    bool found;
    uint8_t page;
    uint8_t record_index;
    BootJournal_Record record;
} BootJournal_Snapshot;
typedef struct {
    void *context;
    BootJournal_Result (*read)(void *, uint8_t, uint16_t, uint8_t *, size_t);
    BootJournal_Result (*program_doubleword)(void *, uint8_t, uint16_t, const uint8_t[8]);
    BootJournal_Result (*erase_page)(void *, uint8_t);
} BootJournal_Flash;
typedef struct {
    uint8_t slot;
    bool recovery;
    BootJournal_Record persisted_record;
} BootJournal_BootDecision;
BootJournal_Result BootJournal_Load(const BootJournal_Flash *flash, BootJournal_Snapshot *snapshot);
BootJournal_Result BootJournal_Append(const BootJournal_Flash *flash,
                                      const BootJournal_Record *record,
                                      BootJournal_Snapshot *snapshot);
BootJournal_Result BootJournal_PrepareBoot(const BootJournal_Flash *flash, uint8_t valid_slot_mask,
                                           uint8_t maximum_trial_attempts,
                                           BootJournal_BootDecision *decision);
BootJournal_Result BootJournal_Confirm(const BootJournal_Flash *flash, uint8_t running_slot,
                                       BootJournal_Snapshot *snapshot);
#endif
