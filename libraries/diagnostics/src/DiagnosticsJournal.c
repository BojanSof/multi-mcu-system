#include "diagnostics/DiagnosticsJournal.h"

#include <limits.h>
#include <string.h>

#define DIAGNOSTICS_JOURNAL_MAGIC 0x314A4644UL
#define DIAGNOSTICS_JOURNAL_CLEAR_MAGIC 0x31434A44UL
#define DIAGNOSTICS_JOURNAL_COMMIT 0x54494D43UL
#define DIAGNOSTICS_JOURNAL_SLOTS (DIAGNOSTICS_JOURNAL_PAGE_SIZE / DIAGNOSTICS_JOURNAL_RECORD_SIZE)

static void DiagnosticsJournal_Write32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
    bytes[2] = (uint8_t)(value >> 16U);
    bytes[3] = (uint8_t)(value >> 24U);
}

static uint32_t DiagnosticsJournal_Read32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) | ((uint32_t)bytes[2] << 16U) |
           ((uint32_t)bytes[3] << 24U);
}

static uint32_t DiagnosticsJournal_Crc(const uint8_t *bytes, size_t length) {
    uint32_t crc = UINT32_MAX;
    size_t index;
    unsigned int bit;
    for (index = 0U; index < length; ++index) {
        crc ^= bytes[index];
        for (bit = 0U; bit < 8U; ++bit) {
            crc = (crc >> 1U) ^ ((crc & 1U) != 0U ? 0xEDB88320UL : 0U);
        }
    }
    return ~crc;
}

void DiagnosticsJournal_EncodeRecord(const DiagnosticsJournal_Record *record,
                                     uint8_t output[DIAGNOSTICS_JOURNAL_PAYLOAD_SIZE]) {
    if ((record == NULL) || (output == NULL)) {
        return;
    }
    (void)memset(output, 0, DIAGNOSTICS_JOURNAL_PAYLOAD_SIZE);
    DiagnosticsJournal_Write32(&output[0], record->generation);
    DiagnosticsJournal_Write32(&output[4], record->boot_count);
    DiagnosticsJournal_Write32(&output[8], record->session);
    DiagnosticsJournal_Write32(&output[12], record->build);
    DiagnosticsJournal_Write32(&output[16], record->time_ms);
    DiagnosticsJournal_Write32(&output[20], record->correlation);
    DiagnosticsJournal_Write32(&output[24], record->detail);
    output[28] = (uint8_t)record->code;
    output[29] = (uint8_t)(record->code >> 8U);
    output[30] = record->reset_reason;
    output[31] = record->severity;
}

static void DiagnosticsJournal_Decode(const uint8_t *bytes, DiagnosticsJournal_Record *record) {
    (void)memset(record, 0, sizeof(*record));
    record->generation = DiagnosticsJournal_Read32(&bytes[0]);
    record->boot_count = DiagnosticsJournal_Read32(&bytes[4]);
    record->session = DiagnosticsJournal_Read32(&bytes[8]);
    record->build = DiagnosticsJournal_Read32(&bytes[12]);
    record->time_ms = DiagnosticsJournal_Read32(&bytes[16]);
    record->correlation = DiagnosticsJournal_Read32(&bytes[20]);
    record->detail = DiagnosticsJournal_Read32(&bytes[24]);
    record->code = (uint16_t)((uint16_t)bytes[28] | ((uint16_t)bytes[29] << 8U));
    record->reset_reason = bytes[30];
    record->severity = bytes[31];
}

static bool DiagnosticsJournal_Valid(const uint8_t *bytes) {
    const uint32_t magic = DiagnosticsJournal_Read32(bytes);
    const uint32_t generation = DiagnosticsJournal_Read32(&bytes[4]);
    return ((magic == DIAGNOSTICS_JOURNAL_MAGIC) || (magic == DIAGNOSTICS_JOURNAL_CLEAR_MAGIC)) &&
           (generation != 0U) &&
           (DiagnosticsJournal_Read32(&bytes[52]) == DiagnosticsJournal_Crc(bytes, 52U)) &&
           (DiagnosticsJournal_Read32(&bytes[56]) == DIAGNOSTICS_JOURNAL_COMMIT) &&
           (DiagnosticsJournal_Read32(&bytes[60]) == ~generation);
}

static bool DiagnosticsJournal_Blank(const uint8_t *bytes) {
    size_t index;
    for (index = 0U; index < DIAGNOSTICS_JOURNAL_RECORD_SIZE; ++index) {
        if (bytes[index] != UINT8_MAX) {
            return false;
        }
    }
    return true;
}

static DiagnosticsJournal_Result DiagnosticsJournal_IoError(DiagnosticsJournal *journal) {
    journal->status.healthy = false;
    if (journal->status.storage_errors != UINT32_MAX) {
        ++journal->status.storage_errors;
    }
    return DIAGNOSTICS_JOURNAL_IO_ERROR;
}

static DiagnosticsJournal_Result DiagnosticsJournal_Scan(DiagnosticsJournal *journal) {
    uint8_t bytes[DIAGNOSTICS_JOURNAL_RECORD_SIZE];
    uint32_t page;
    uint32_t slot;
    uint32_t valid_count = 0U;
    uint32_t visible_count = 0U;
    journal->status.latest_generation = 0U;
    journal->clear_generation = 0U;
    journal->newest_page = 0U;
    for (page = 0U; page < DIAGNOSTICS_JOURNAL_PAGE_COUNT; ++page) {
        journal->next_slot[page] = 0U;
        for (slot = 0U; slot < DIAGNOSTICS_JOURNAL_SLOTS; ++slot) {
            if (!journal->storage.read(journal->storage.context, page,
                                       slot * DIAGNOSTICS_JOURNAL_RECORD_SIZE, bytes,
                                       sizeof(bytes))) {
                return DiagnosticsJournal_IoError(journal);
            }
            if (!DiagnosticsJournal_Blank(bytes)) {
                journal->next_slot[page] = slot + 1U;
            }
            if (DiagnosticsJournal_Valid(bytes)) {
                const uint32_t generation = DiagnosticsJournal_Read32(&bytes[4]);
                ++valid_count;
                if (generation > journal->status.latest_generation) {
                    journal->status.latest_generation = generation;
                    journal->newest_page = page;
                }
                if ((DiagnosticsJournal_Read32(bytes) == DIAGNOSTICS_JOURNAL_CLEAR_MAGIC) &&
                    (generation > journal->clear_generation)) {
                    journal->clear_generation = generation;
                }
            }
        }
    }
    /* A second pass counts only fault records newer than the last clear. The
     * tombstone cannot be erased before all records preceding it have aged out. */
    for (page = 0U; page < DIAGNOSTICS_JOURNAL_PAGE_COUNT; ++page) {
        for (slot = 0U; slot < DIAGNOSTICS_JOURNAL_SLOTS; ++slot) {
            if (!journal->storage.read(journal->storage.context, page,
                                       slot * DIAGNOSTICS_JOURNAL_RECORD_SIZE, bytes,
                                       sizeof(bytes))) {
                return DiagnosticsJournal_IoError(journal);
            }
            if (DiagnosticsJournal_Valid(bytes) &&
                (DiagnosticsJournal_Read32(bytes) == DIAGNOSTICS_JOURNAL_MAGIC) &&
                (DiagnosticsJournal_Read32(&bytes[4]) > journal->clear_generation)) {
                ++visible_count;
            }
        }
    }
    journal->status.record_count = visible_count;
    journal->status.overwritten = journal->status.latest_generation - valid_count;
    journal->status.healthy = true;
    return DIAGNOSTICS_JOURNAL_OK;
}

DiagnosticsJournal_Result DiagnosticsJournal_Initialize(DiagnosticsJournal *journal,
                                                        const DiagnosticsJournal_Storage *storage) {
    if ((journal == NULL) || (storage == NULL) || (storage->read == NULL) ||
        (storage->program == NULL) || (storage->erase == NULL)) {
        return DIAGNOSTICS_JOURNAL_INVALID_ARGUMENT;
    }
    (void)memset(journal, 0, sizeof(*journal));
    journal->storage = *storage;
    journal->initialized = true;
    return DiagnosticsJournal_Scan(journal);
}

static DiagnosticsJournal_Result DiagnosticsJournal_Write(DiagnosticsJournal *journal,
                                                          const DiagnosticsJournal_Record *record,
                                                          bool clear) {
    uint8_t bytes[DIAGNOSTICS_JOURNAL_RECORD_SIZE];
    uint32_t page;
    uint32_t offset;
    uint32_t written;
    DiagnosticsJournal_Record stored = *record;
    DiagnosticsJournal_Result result = DiagnosticsJournal_Scan(journal);
    if (result != DIAGNOSTICS_JOURNAL_OK) {
        return result;
    }
    if (journal->status.latest_generation == UINT32_MAX) {
        return DIAGNOSTICS_JOURNAL_GENERATION_EXHAUSTED;
    }
    stored.generation = journal->status.latest_generation + 1U;
    page = journal->newest_page;
    if (journal->next_slot[page] >= DIAGNOSTICS_JOURNAL_SLOTS) {
        page = 1U - page;
        /* The page with the newest valid record is never erased here. */
        if (!journal->storage.erase(journal->storage.context, page)) {
            return DiagnosticsJournal_IoError(journal);
        }
        journal->next_slot[page] = 0U;
    }
    offset = journal->next_slot[page] * DIAGNOSTICS_JOURNAL_RECORD_SIZE;
    (void)memset(bytes, 0, sizeof(bytes));
    DiagnosticsJournal_Write32(bytes,
                               clear ? DIAGNOSTICS_JOURNAL_CLEAR_MAGIC : DIAGNOSTICS_JOURNAL_MAGIC);
    DiagnosticsJournal_EncodeRecord(&stored, &bytes[4]);
    DiagnosticsJournal_Write32(&bytes[52], DiagnosticsJournal_Crc(bytes, 52U));
    DiagnosticsJournal_Write32(&bytes[56], DIAGNOSTICS_JOURNAL_COMMIT);
    DiagnosticsJournal_Write32(&bytes[60], ~stored.generation);
    for (written = 0U; written < DIAGNOSTICS_JOURNAL_RECORD_SIZE; written += 8U) {
        if (!journal->storage.program(journal->storage.context, page, offset + written,
                                      &bytes[written])) {
            return DiagnosticsJournal_IoError(journal);
        }
    }
    result = DiagnosticsJournal_Scan(journal);
    if ((result == DIAGNOSTICS_JOURNAL_OK) &&
        (journal->status.latest_generation != stored.generation)) {
        return DiagnosticsJournal_IoError(journal);
    }
    return result;
}

DiagnosticsJournal_Result DiagnosticsJournal_Append(DiagnosticsJournal *journal,
                                                    const DiagnosticsJournal_Record *record) {
    if ((journal == NULL) || (record == NULL) || !journal->initialized) {
        return DIAGNOSTICS_JOURNAL_INVALID_ARGUMENT;
    }
    return DiagnosticsJournal_Write(journal, record, false);
}

DiagnosticsJournal_Result DiagnosticsJournal_ReadAfter(DiagnosticsJournal *journal, uint32_t cursor,
                                                       DiagnosticsJournal_Record *record,
                                                       bool *found) {
    uint8_t bytes[DIAGNOSTICS_JOURNAL_RECORD_SIZE];
    uint32_t page;
    uint32_t slot;
    uint32_t best = UINT32_MAX;
    DiagnosticsJournal_Result result;
    if (found != NULL) {
        *found = false;
    }
    if ((journal == NULL) || (record == NULL) || (found == NULL) || !journal->initialized) {
        return DIAGNOSTICS_JOURNAL_INVALID_ARGUMENT;
    }
    result = DiagnosticsJournal_Scan(journal);
    if (result != DIAGNOSTICS_JOURNAL_OK) {
        return result;
    }
    for (page = 0U; page < DIAGNOSTICS_JOURNAL_PAGE_COUNT; ++page) {
        for (slot = 0U; slot < DIAGNOSTICS_JOURNAL_SLOTS; ++slot) {
            uint32_t generation;
            if (!journal->storage.read(journal->storage.context, page,
                                       slot * DIAGNOSTICS_JOURNAL_RECORD_SIZE, bytes,
                                       sizeof(bytes))) {
                return DiagnosticsJournal_IoError(journal);
            }
            generation = DiagnosticsJournal_Read32(&bytes[4]);
            if (DiagnosticsJournal_Valid(bytes) &&
                (DiagnosticsJournal_Read32(bytes) == DIAGNOSTICS_JOURNAL_MAGIC) &&
                (generation > cursor) && (generation > journal->clear_generation) &&
                (!*found || (generation < best))) {
                best = generation;
                DiagnosticsJournal_Decode(&bytes[4], record);
                *found = true;
            }
        }
    }
    return DIAGNOSTICS_JOURNAL_OK;
}

DiagnosticsJournal_Result DiagnosticsJournal_Clear(DiagnosticsJournal *journal) {
    const DiagnosticsJournal_Record empty = {0};
    if ((journal == NULL) || !journal->initialized) {
        return DIAGNOSTICS_JOURNAL_INVALID_ARGUMENT;
    }
    return DiagnosticsJournal_Write(journal, &empty, true);
}

DiagnosticsJournal_Status DiagnosticsJournal_GetStatus(const DiagnosticsJournal *journal) {
    const DiagnosticsJournal_Status empty = {0};
    return journal != NULL ? journal->status : empty;
}
