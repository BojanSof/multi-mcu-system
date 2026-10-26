#ifndef DIAGNOSTICS_JOURNAL_H
#define DIAGNOSTICS_JOURNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DIAGNOSTICS_JOURNAL_PAGE_SIZE 2048U
#define DIAGNOSTICS_JOURNAL_PAGE_COUNT 2U
#define DIAGNOSTICS_JOURNAL_RECORD_SIZE 64U
#define DIAGNOSTICS_JOURNAL_PAYLOAD_SIZE 48U

typedef struct {
    uint32_t generation;
    uint32_t boot_count;
    uint32_t session;
    uint32_t build;
    uint32_t time_ms;
    uint32_t correlation;
    uint32_t detail;
    uint16_t code;
    uint8_t reset_reason;
    uint8_t severity;
} DiagnosticsJournal_Record;

/* Callbacks are serialized by the caller, never called from an ISR. Program
 * writes exactly one aligned doubleword and must not erase or overwrite bits.
 * Both page numbers are relative to the dedicated journal allocation (0 or 1). */
typedef struct {
    void *context;
    bool (*read)(void *context, uint32_t page, uint32_t offset, uint8_t *data, size_t length);
    bool (*program)(void *context, uint32_t page, uint32_t offset, const uint8_t data[8]);
    bool (*erase)(void *context, uint32_t page);
} DiagnosticsJournal_Storage;

typedef enum {
    DIAGNOSTICS_JOURNAL_OK = 0,
    DIAGNOSTICS_JOURNAL_INVALID_ARGUMENT,
    DIAGNOSTICS_JOURNAL_IO_ERROR,
    DIAGNOSTICS_JOURNAL_GENERATION_EXHAUSTED
} DiagnosticsJournal_Result;

typedef struct {
    bool healthy;
    uint32_t overwritten;
    uint32_t storage_errors;
    uint32_t latest_generation;
    uint32_t record_count;
} DiagnosticsJournal_Status;

typedef struct {
    DiagnosticsJournal_Storage storage;
    DiagnosticsJournal_Status status;
    uint32_t clear_generation;
    uint32_t newest_page;
    uint32_t next_slot[DIAGNOSTICS_JOURNAL_PAGE_COUNT];
    bool initialized;
} DiagnosticsJournal;

DiagnosticsJournal_Result DiagnosticsJournal_Initialize(DiagnosticsJournal *journal,
                                                        const DiagnosticsJournal_Storage *storage);
DiagnosticsJournal_Result DiagnosticsJournal_Append(DiagnosticsJournal *journal,
                                                    const DiagnosticsJournal_Record *record);
DiagnosticsJournal_Result DiagnosticsJournal_ReadAfter(DiagnosticsJournal *journal, uint32_t cursor,
                                                       DiagnosticsJournal_Record *record,
                                                       bool *found);
/* Clear is a committed logical tombstone: interrupted clear leaves either old
 * history or cleared history; successful clear cannot resurrect older records. */
DiagnosticsJournal_Result DiagnosticsJournal_Clear(DiagnosticsJournal *journal);
DiagnosticsJournal_Status DiagnosticsJournal_GetStatus(const DiagnosticsJournal *journal);
/* Stable little-endian wire payload. Bytes32..47 are reserved and encoded zero. */
void DiagnosticsJournal_EncodeRecord(const DiagnosticsJournal_Record *record,
                                     uint8_t output[DIAGNOSTICS_JOURNAL_PAYLOAD_SIZE]);

#endif
