#include "diagnostics/DiagnosticsJournal.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t pages[DIAGNOSTICS_JOURNAL_PAGE_COUNT][DIAGNOSTICS_JOURNAL_PAGE_SIZE];
    int remaining;
    size_t partial;
    bool read_failure;
    bool silent_corruption;
    uint32_t erases;
} TestFlash;

static bool Test_Read(void *context, uint32_t page, uint32_t offset, uint8_t *data, size_t length) {
    TestFlash *flash = context;
    assert(page < DIAGNOSTICS_JOURNAL_PAGE_COUNT);
    assert(offset + length <= DIAGNOSTICS_JOURNAL_PAGE_SIZE);
    if (flash->read_failure) {
        return false;
    }
    (void)memcpy(data, &flash->pages[page][offset], length);
    return true;
}

static bool Test_Program(void *context, uint32_t page, uint32_t offset, const uint8_t data[8]) {
    TestFlash *flash = context;
    size_t index;
    const bool interrupted = flash->remaining == 0;
    const size_t length = interrupted ? flash->partial : 8U;
    assert(page < DIAGNOSTICS_JOURNAL_PAGE_COUNT);
    assert(offset + 8U <= DIAGNOSTICS_JOURNAL_PAGE_SIZE);
    assert((offset % 8U) == 0U);
    for (index = 0U; index < length; ++index) {
        assert((flash->pages[page][offset + index] & data[index]) == data[index]);
        flash->pages[page][offset + index] &= data[index];
    }
    if (flash->silent_corruption) {
        flash->pages[page][offset] &= 0xFEU;
    }
    if (interrupted) {
        return false;
    }
    if (flash->remaining > 0) {
        --flash->remaining;
    }
    return true;
}

static bool Test_Erase(void *context, uint32_t page) {
    TestFlash *flash = context;
    const bool interrupted = flash->remaining == 0;
    assert(page < DIAGNOSTICS_JOURNAL_PAGE_COUNT);
    (void)memset(flash->pages[page], 0xFF,
                 interrupted ? flash->partial : DIAGNOSTICS_JOURNAL_PAGE_SIZE);
    if (interrupted) {
        return false;
    }
    ++flash->erases;
    if (flash->remaining > 0) {
        --flash->remaining;
    }
    return true;
}

static void Test_Blank(TestFlash *flash) {
    (void)memset(flash, 0, sizeof(*flash));
    (void)memset(flash->pages, 0xFF, sizeof(flash->pages));
    flash->remaining = -1;
}

static void Test_Open(TestFlash *flash, DiagnosticsJournal *journal) {
    const DiagnosticsJournal_Storage storage = {flash, Test_Read, Test_Program, Test_Erase};
    assert(DiagnosticsJournal_Initialize(journal, &storage) == DIAGNOSTICS_JOURNAL_OK);
}

static void Test_Append(DiagnosticsJournal *journal, uint32_t value) {
    DiagnosticsJournal_Record record = {0};
    record.boot_count = value;
    record.session = 123U;
    record.build = 456U;
    record.time_ms = 789U;
    record.correlation = 987U;
    record.detail = 654U;
    record.code = 0x1234U;
    record.reset_reason = 2U;
    record.severity = 3U;
    assert(DiagnosticsJournal_Append(journal, &record) == DIAGNOSTICS_JOURNAL_OK);
}

static uint32_t Test_Count(DiagnosticsJournal *journal, uint32_t *latest) {
    DiagnosticsJournal_Record record;
    uint32_t cursor = 0U;
    uint32_t count = 0U;
    bool found = false;
    *latest = 0U;
    do {
        assert(DiagnosticsJournal_ReadAfter(journal, cursor, &record, &found) ==
               DIAGNOSTICS_JOURNAL_OK);
        if (found) {
            assert(record.generation > cursor);
            cursor = record.generation;
            *latest = record.boot_count;
            ++count;
            assert(count <= 64U);
        }
    } while (found);
    return count;
}

static void Test_BasicAndRollover(void) {
    TestFlash flash;
    DiagnosticsJournal journal;
    DiagnosticsJournal_Record record;
    DiagnosticsJournal_Status status;
    uint8_t encoded[DIAGNOSTICS_JOURNAL_PAYLOAD_SIZE];
    uint32_t value;
    uint32_t latest;
    bool found;
    Test_Blank(&flash);
    Test_Open(&flash, &journal);
    assert(Test_Count(&journal, &latest) == 0U);
    Test_Append(&journal, 1U);
    assert(DiagnosticsJournal_ReadAfter(&journal, 0U, &record, &found) == DIAGNOSTICS_JOURNAL_OK);
    assert(found && record.generation == 1U && record.boot_count == 1U);
    assert(record.session == 123U && record.build == 456U && record.time_ms == 789U);
    assert(record.correlation == 987U && record.detail == 654U && record.code == 0x1234U);
    assert(record.reset_reason == 2U && record.severity == 3U);
    DiagnosticsJournal_EncodeRecord(&record, encoded);
    assert(encoded[0] == 1U && encoded[1] == 0U);
    assert(encoded[28] == 0x34U && encoded[29] == 0x12U);
    for (value = 32U; value < sizeof(encoded); ++value) {
        assert(encoded[value] == 0U);
    }
    for (value = 2U; value <= 130U; ++value) {
        Test_Append(&journal, value);
    }
    Test_Open(&flash, &journal);
    assert(Test_Count(&journal, &latest) == 34U);
    assert(latest == 130U);
    status = DiagnosticsJournal_GetStatus(&journal);
    assert(status.healthy && status.latest_generation == 130U);
    assert(status.record_count == 34U && status.overwritten == 96U);
    assert(flash.erases == 4U);
}

static void Test_InterruptedAppend(uint32_t baseline_count) {
    TestFlash baseline;
    DiagnosticsJournal journal;
    uint32_t value;
    int step;
    size_t partial;
    Test_Blank(&baseline);
    Test_Open(&baseline, &journal);
    for (value = 1U; value <= baseline_count; ++value) {
        Test_Append(&journal, value);
    }
    for (step = 0; step <= 9; ++step) {
        for (partial = 0U; partial < 8U; ++partial) {
            TestFlash flash = baseline;
            DiagnosticsJournal_Record next = {0};
            DiagnosticsJournal_Result result;
            uint32_t latest;
            next.boot_count = baseline_count + 1U;
            Test_Open(&flash, &journal);
            flash.remaining = step;
            flash.partial = partial;
            result = DiagnosticsJournal_Append(&journal, &next);
            if (result != DIAGNOSTICS_JOURNAL_OK) {
                assert(result == DIAGNOSTICS_JOURNAL_IO_ERROR);
                assert(!DiagnosticsJournal_GetStatus(&journal).healthy);
                assert(DiagnosticsJournal_GetStatus(&journal).storage_errors == 1U);
            }
            flash.remaining = -1;
            Test_Open(&flash, &journal);
            (void)Test_Count(&journal, &latest);
            assert(latest == baseline_count || latest == baseline_count + 1U);
            Test_Append(&journal, 1000U);
            (void)Test_Count(&journal, &latest);
            assert(latest == 1000U);
        }
    }
}

static void Test_InterruptedErase(void) {
    TestFlash baseline;
    DiagnosticsJournal journal;
    uint32_t value;
    size_t partial;
    Test_Blank(&baseline);
    Test_Open(&baseline, &journal);
    for (value = 1U; value <= 64U; ++value) {
        Test_Append(&journal, value);
    }
    for (partial = 0U; partial <= DIAGNOSTICS_JOURNAL_PAGE_SIZE; partial += 8U) {
        TestFlash flash = baseline;
        DiagnosticsJournal_Record record = {0};
        uint32_t latest;
        Test_Open(&flash, &journal);
        flash.remaining = 0;
        flash.partial = partial;
        assert(DiagnosticsJournal_Append(&journal, &record) == DIAGNOSTICS_JOURNAL_IO_ERROR);
        flash.remaining = -1;
        Test_Open(&flash, &journal);
        (void)Test_Count(&journal, &latest);
        assert(latest == 64U);
        Test_Append(&journal, 65U);
        (void)Test_Count(&journal, &latest);
        assert(latest == 65U);
    }
}

static void Test_ClearAndInterruption(void) {
    TestFlash baseline;
    DiagnosticsJournal journal;
    int step;
    size_t partial;
    uint32_t value;
    Test_Blank(&baseline);
    Test_Open(&baseline, &journal);
    Test_Append(&journal, 1U);
    for (step = 0; step <= 8; ++step) {
        for (partial = 0U; partial < 8U; ++partial) {
            TestFlash flash = baseline;
            uint32_t latest;
            uint32_t count;
            Test_Open(&flash, &journal);
            flash.remaining = step;
            flash.partial = partial;
            (void)DiagnosticsJournal_Clear(&journal);
            flash.remaining = -1;
            Test_Open(&flash, &journal);
            count = Test_Count(&journal, &latest);
            assert(count == 0U || count == 1U);
            assert(DiagnosticsJournal_Clear(&journal) == DIAGNOSTICS_JOURNAL_OK);
            assert(Test_Count(&journal, &latest) == 0U);
        }
    }
    Test_Open(&baseline, &journal);
    assert(DiagnosticsJournal_Clear(&journal) == DIAGNOSTICS_JOURNAL_OK);
    /* Rollover eventually removes the clear tombstone, but cannot resurrect
     * the old generation because its page must have been erased first. */
    for (value = 2U; value <= 100U; ++value) {
        uint32_t latest;
        DiagnosticsJournal_Record record;
        bool found;
        Test_Append(&journal, value);
        Test_Open(&baseline, &journal);
        assert(DiagnosticsJournal_ReadAfter(&journal, 0U, &record, &found) ==
               DIAGNOSTICS_JOURNAL_OK);
        assert(found && record.boot_count >= 2U);
        (void)Test_Count(&journal, &latest);
        assert(latest == value);
    }
}

static void Test_CorruptionAndErrors(void) {
    TestFlash baseline;
    DiagnosticsJournal journal;
    size_t byte;
    uint32_t latest;
    DiagnosticsJournal_Record record = {0};
    const DiagnosticsJournal_Storage bad = {0};
    bool found = true;
    Test_Blank(&baseline);
    Test_Open(&baseline, &journal);
    Test_Append(&journal, 1U);
    Test_Append(&journal, 2U);
    for (byte = 0U; byte < DIAGNOSTICS_JOURNAL_RECORD_SIZE; ++byte) {
        TestFlash flash = baseline;
        flash.pages[0][DIAGNOSTICS_JOURNAL_RECORD_SIZE + byte] ^= 1U;
        Test_Open(&flash, &journal);
        assert(Test_Count(&journal, &latest) == 1U);
        assert(latest == 1U);
        Test_Append(&journal, 3U);
        (void)Test_Count(&journal, &latest);
        assert(latest == 3U);
    }
    Test_Open(&baseline, &journal);
    baseline.read_failure = true;
    assert(DiagnosticsJournal_ReadAfter(&journal, 0U, &record, &found) ==
           DIAGNOSTICS_JOURNAL_IO_ERROR);
    assert(!found && !DiagnosticsJournal_GetStatus(&journal).healthy);
    baseline.read_failure = false;
    assert(DiagnosticsJournal_ReadAfter(&journal, 0U, &record, &found) == DIAGNOSTICS_JOURNAL_OK);
    assert(found && DiagnosticsJournal_GetStatus(&journal).healthy);
    assert(DiagnosticsJournal_GetStatus(&journal).storage_errors == 1U);
    assert(DiagnosticsJournal_Initialize(NULL, &bad) == DIAGNOSTICS_JOURNAL_INVALID_ARGUMENT);
    assert(DiagnosticsJournal_Initialize(&journal, &bad) == DIAGNOSTICS_JOURNAL_INVALID_ARGUMENT);
    assert(DiagnosticsJournal_Append(NULL, &record) == DIAGNOSTICS_JOURNAL_INVALID_ARGUMENT);
    assert(DiagnosticsJournal_Clear(NULL) == DIAGNOSTICS_JOURNAL_INVALID_ARGUMENT);
}

int main(void) {
    Test_BasicAndRollover();
    Test_InterruptedAppend(1U);
    Test_InterruptedAppend(32U);
    Test_InterruptedAppend(64U);
    Test_InterruptedErase();
    Test_ClearAndInterruption();
    Test_CorruptionAndErrors();
    (void)puts("persistent diagnostic journal: PASS");
    return 0;
}
