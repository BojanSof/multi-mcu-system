#include "stm32u083_bootloader/BootJournal.h"
#include <stdio.h>
#include <string.h>
typedef struct {
    uint8_t pages[2][BOOT_JOURNAL_PAGE_SIZE];
    uint32_t op, fail;
} TestFlash;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            printf("FAIL line %d: %s\n", __LINE__, #x);                                            \
            return false;                                                                          \
        }                                                                                          \
    } while (0)
static void Init(TestFlash *f) {
    memset(f->pages, 0xFF, sizeof(f->pages));
    f->op = 0U;
    f->fail = 0U;
}
static BootJournal_Result Read(void *c, uint8_t p, uint16_t o, uint8_t *d, size_t n) {
    TestFlash *f = c;
    if (p >= 2U || (size_t)o + n > BOOT_JOURNAL_PAGE_SIZE)
        return BOOT_JOURNAL_ERROR_IO;
    memcpy(d, f->pages[p] + o, n);
    return BOOT_JOURNAL_OK;
}
static bool Fail(TestFlash *f) {
    f->op++;
    return f->fail != 0U && f->op == f->fail;
}
static BootJournal_Result Program(void *c, uint8_t p, uint16_t o, const uint8_t d[8]) {
    TestFlash *f = c;
    if (p >= 2U || (o & 7U) || (size_t)o + 8U > BOOT_JOURNAL_PAGE_SIZE)
        return BOOT_JOURNAL_ERROR_IO;
    if (Fail(f)) {
        for (size_t i = 0U; i < 4U; ++i)
            f->pages[p][o + i] &= d[i];
        return BOOT_JOURNAL_ERROR_POWER_LOSS;
    }
    for (size_t i = 0; i < 8U; i++) {
        if (((uint8_t)~f->pages[p][o + i] & d[i]) != 0U)
            return BOOT_JOURNAL_ERROR_IO;
        f->pages[p][o + i] &= d[i];
    }
    return BOOT_JOURNAL_OK;
}
static BootJournal_Result Erase(void *c, uint8_t p) {
    TestFlash *f = c;
    if (p >= 2U)
        return BOOT_JOURNAL_ERROR_IO;
    if (Fail(f)) {
        memset(f->pages[p], 0xA5, BOOT_JOURNAL_PAGE_SIZE);
        return BOOT_JOURNAL_ERROR_POWER_LOSS;
    }
    memset(f->pages[p], 0xFF, BOOT_JOURNAL_PAGE_SIZE);
    return BOOT_JOURNAL_OK;
}
static BootJournal_Flash Interface(TestFlash *f) {
    BootJournal_Flash i = {f, Read, Program, Erase};
    return i;
}
static BootJournal_Record Record(BootJournal_State s, uint8_t confirmed, uint8_t selected) {
    BootJournal_Record r = {0U, s, confirmed, selected, 0U};
    return r;
}
static bool Blank(void) {
    TestFlash m;
    Init(&m);
    BootJournal_Flash f = Interface(&m);
    BootJournal_BootDecision d;
    CHECK(BootJournal_PrepareBoot(&f, 3U, 2U, &d) == BOOT_JOURNAL_OK);
    CHECK(d.recovery && d.slot == BOOT_JOURNAL_SLOT_NONE);
    return true;
}
static bool Confirm(void) {
    TestFlash m;
    Init(&m);
    BootJournal_Flash f = Interface(&m);
    BootJournal_Snapshot s;
    BootJournal_Record a = Record(BOOT_JOURNAL_STATE_CONFIRMED, 0U, 0U),
                       p = Record(BOOT_JOURNAL_STATE_PENDING, 0U, 1U);
    CHECK(BootJournal_Append(&f, &a, &s) == 0);
    CHECK(BootJournal_Append(&f, &p, &s) == 0);
    BootJournal_BootDecision d;
    CHECK(BootJournal_PrepareBoot(&f, 3U, 2U, &d) == 0);
    CHECK(d.slot == 1U && d.persisted_record.state == BOOT_JOURNAL_STATE_TRIAL);
    CHECK(BootJournal_Confirm(&f, 1U, &s) == 0);
    CHECK(s.record.state == BOOT_JOURNAL_STATE_CONFIRMED && s.record.confirmed_slot == 1U);
    return true;
}
static bool Rollback(void) {
    TestFlash m;
    Init(&m);
    BootJournal_Flash f = Interface(&m);
    BootJournal_Snapshot s;
    BootJournal_Record p = Record(BOOT_JOURNAL_STATE_PENDING, 0U, 1U);
    CHECK(BootJournal_Append(&f, &p, &s) == 0);
    BootJournal_BootDecision d;
    CHECK(BootJournal_PrepareBoot(&f, 3U, 2U, &d) == 0 && d.slot == 1U);
    CHECK(BootJournal_PrepareBoot(&f, 3U, 2U, &d) == 0 && d.slot == 1U);
    CHECK(BootJournal_PrepareBoot(&f, 3U, 2U, &d) == 0 && d.slot == 0U);
    CHECK(d.persisted_record.state == BOOT_JOURNAL_STATE_CONFIRMED);
    return true;
}
static bool Corruption(void) {
    TestFlash m;
    Init(&m);
    BootJournal_Flash f = Interface(&m);
    BootJournal_Snapshot s;
    BootJournal_Record a = Record(BOOT_JOURNAL_STATE_CONFIRMED, 0U, 0U),
                       b = Record(BOOT_JOURNAL_STATE_PENDING, 0U, 1U);
    CHECK(BootJournal_Append(&f, &a, &s) == 0);
    CHECK(BootJournal_Append(&f, &b, &s) == 0);
    m.pages[0][BOOT_JOURNAL_RECORD_SIZE + 20U] ^= 1U;
    CHECK(BootJournal_Load(&f, &s) == 0);
    CHECK(s.record.state == BOOT_JOURNAL_STATE_CONFIRMED);
    return true;
}
static bool InvalidPending(void) {
    TestFlash m;
    Init(&m);
    BootJournal_Flash f = Interface(&m);
    BootJournal_Snapshot s;
    BootJournal_Record p = Record(BOOT_JOURNAL_STATE_PENDING, 0U, 1U);
    CHECK(BootJournal_Append(&f, &p, &s) == 0);
    BootJournal_BootDecision d;
    CHECK(BootJournal_PrepareBoot(&f, 1U, 2U, &d) == 0);
    CHECK(d.slot == 0U && d.persisted_record.state == BOOT_JOURNAL_STATE_INVALID);
    return true;
}
static bool Wrap(void) {
    TestFlash m;
    Init(&m);
    BootJournal_Flash f = Interface(&m);
    BootJournal_Snapshot s;
    BootJournal_Record r = Record(BOOT_JOURNAL_STATE_CONFIRMED, 0U, 0U);
    r.generation = 0xFFFFFFFEUL;
    CHECK(BootJournal_Append(&f, &r, &s) == 0);
    CHECK(BootJournal_Append(&f, &r, &s) == 0 && s.record.generation == 0xFFFFFFFFUL);
    CHECK(BootJournal_Append(&f, &r, &s) == 0 && s.record.generation == 0U);
    CHECK(BootJournal_Load(&f, &s) == 0 && s.record.generation == 0U);
    return true;
}
static bool PowerLoss(void) {
    TestFlash base;
    Init(&base);
    BootJournal_Flash f = Interface(&base);
    BootJournal_Snapshot s;
    BootJournal_Record r = Record(BOOT_JOURNAL_STATE_CONFIRMED, 0U, 0U);
    CHECK(BootJournal_Append(&f, &r, &s) == 0);
    for (uint32_t fail = 1U; fail <= 8U; ++fail) {
        TestFlash interrupted = base;
        interrupted.op = 0U;
        interrupted.fail = fail;
        f = Interface(&interrupted);
        CHECK(BootJournal_Append(&f, &r, &s) == BOOT_JOURNAL_ERROR_POWER_LOSS);
        interrupted.fail = 0U;
        CHECK(BootJournal_Load(&f, &s) == 0 && s.record.generation == 0U);
    }
    f = Interface(&base);
    for (uint32_t i = 1U; i < BOOT_JOURNAL_RECORDS_PER_PAGE; i++)
        CHECK(BootJournal_Append(&f, &r, &s) == 0);
    uint32_t old = s.record.generation;
    for (uint32_t fail = 1U; fail <= 9U; fail++) {
        TestFlash m = base;
        m.op = 0U;
        m.fail = fail;
        f = Interface(&m);
        CHECK(BootJournal_Append(&f, &r, &s) == BOOT_JOURNAL_ERROR_POWER_LOSS);
        m.fail = 0U;
        CHECK(BootJournal_Load(&f, &s) == 0);
        CHECK(s.record.generation == old || s.record.generation == old + 1U);
    }
    return true;
}
int main(void) {
    struct {
        const char *name;
        bool (*fn)(void);
    } tests[] = {{"blank", Blank},
                 {"confirm", Confirm},
                 {"rollback", Rollback},
                 {"corruption", Corruption},
                 {"invalid pending", InvalidPending},
                 {"wrap", Wrap},
                 {"power loss", PowerLoss}};
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        if (!tests[i].fn())
            return 1;
        printf("PASS %s\n", tests[i].name);
    }
    return 0;
}
