#include "stm32u083_bootloader/BootEngine.h"
#include <stdio.h>
#include <string.h>
typedef struct {
    uint8_t journal[2][2048];
    bool valid[2], rebooted, fail_write;
    uint8_t erased_slot;
    uint32_t written, write_calls, erase_calls;
} Model;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            printf("FAIL line %d: %s\n", __LINE__, #x);                                            \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
static BootJournal_Result Read(void *c, uint8_t p, uint16_t o, uint8_t *d, size_t n) {
    Model *m = c;
    memcpy(d, m->journal[p] + o, n);
    return BOOT_JOURNAL_OK;
}
static BootJournal_Result Program(void *c, uint8_t p, uint16_t o, const uint8_t d[8]) {
    Model *m = c;
    for (size_t i = 0; i < 8U; i++)
        m->journal[p][o + i] &= d[i];
    return BOOT_JOURNAL_OK;
}
static BootJournal_Result ErasePage(void *c, uint8_t p) {
    Model *m = c;
    memset(m->journal[p], 0xFF, 2048U);
    return BOOT_JOURNAL_OK;
}
static BootProtocol_Status EraseSlot(void *c, uint8_t s) {
    Model *m = c;
    m->erased_slot = s;
    m->erase_calls++;
    m->written = 0U;
    m->valid[s] = false;
    return BOOT_PROTOCOL_STATUS_OK;
}
static BootProtocol_Status Write(void *c, uint8_t s, uint32_t o, const uint8_t *d, size_t n) {
    Model *m = c;
    (void)s;
    (void)d;
    m->write_calls++;
    if (m->fail_write)
        return BOOT_PROTOCOL_STATUS_FLASH;
    if (o != m->written)
        return BOOT_PROTOCOL_STATUS_SEQUENCE;
    m->written += (uint32_t)n;
    return BOOT_PROTOCOL_STATUS_OK;
}
static bool Validate(void *c, uint8_t s, uint32_t size) {
    Model *m = c;
    return size == 0U ? m->valid[s] : (s == 1U && size == 8U);
}
static void Reboot(void *c) { ((Model *)c)->rebooted = true; }
static BootProtocol_Message Request(uint8_t command, uint8_t slot) {
    static uint16_t sequence = 1U;
    BootProtocol_Message q = {0};
    q.command = command;
    q.flags = slot;
    q.sequence = sequence++;
    return q;
}
int main(void) {
    Model m;
    memset(&m, 0, sizeof(m));
    memset(m.journal, 0xFF, sizeof(m.journal));
    m.valid[0] = true;
    BootJournal_Flash j = {&m, Read, Program, ErasePage};
    BootEngine_Storage s = {&m, EraseSlot, Write, Validate, Reboot};
    BootJournal_Record confirmed = {0U, BOOT_JOURNAL_STATE_CONFIRMED, 0U, 0U, 0U};
    BootJournal_Snapshot snap;
    CHECK(BootJournal_Append(&j, &confirmed, &snap) == 0);
    BootEngine_Context engine;
    BootEngine_Init(&engine, &j, &s);
    BootProtocol_Message q = Request(BOOT_PROTOCOL_COMMAND_ERASE_INACTIVE, 0U), r;
    CHECK(BootEngine_Process(&engine, &q, &r) == BOOT_PROTOCOL_STATUS_STATE);
    q = Request(BOOT_PROTOCOL_COMMAND_ERASE_INACTIVE, 1U);
    CHECK(BootEngine_Process(&engine, &q, &r) == 0 && m.erased_slot == 1U);
    BootProtocol_Message old_erase = q;
    q = Request(BOOT_PROTOCOL_COMMAND_WRITE_CHUNK, 1U);
    q.payload_length = 8U;
    memset(q.payload, 0xA5, 8U);
    CHECK(BootEngine_Process(&engine, &q, &r) == 0 && m.written == 8U);
    BootProtocol_Message duplicate_write = q, duplicate_response;
    CHECK(BootEngine_Process(&engine, &duplicate_write, &duplicate_response) == 0);
    CHECK(m.written == 8U && m.write_calls == 1U && duplicate_response.status == r.status);
    CHECK(BootEngine_Process(&engine, &old_erase, &duplicate_response) ==
          BOOT_PROTOCOL_STATUS_SEQUENCE);
    CHECK(m.erase_calls == 1U);
    q = Request(BOOT_PROTOCOL_COMMAND_FINALIZE, 1U);
    q.operation_offset = 8U;
    CHECK(BootEngine_Process(&engine, &q, &r) == 0);
    q = Request(BOOT_PROTOCOL_COMMAND_SET_PENDING, 1U);
    CHECK(BootEngine_Process(&engine, &q, &r) == 0);
    CHECK(BootJournal_Load(&j, &snap) == 0 && snap.record.state == BOOT_JOURNAL_STATE_PENDING &&
          snap.record.confirmed_slot == 0U);
    const uint32_t pending_generation = snap.record.generation;
    CHECK(BootEngine_Process(&engine, &q, &duplicate_response) == 0);
    CHECK(BootJournal_Load(&j, &snap) == 0 && snap.record.generation == pending_generation);
    m.valid[1] = true;
    BootEngine_SetActiveSlot(&engine, 1U);
    q = Request(BOOT_PROTOCOL_COMMAND_QUERY_SLOTS, 0U);
    CHECK(BootEngine_Process(&engine, &q, &r) == 0 && r.payload[0] == 3U && r.payload[4] == 1U);
    q = Request(BOOT_PROTOCOL_COMMAND_REBOOT, 0U);
    CHECK(BootEngine_Process(&engine, &q, &r) == 0 && m.rebooted);
    q = Request(BOOT_PROTOCOL_COMMAND_QUERY_SLOTS, 0U);
    q.flags = BOOT_PROTOCOL_FLAG_RESPONSE;
    CHECK(BootEngine_Process(&engine, &q, &r) == BOOT_PROTOCOL_STATUS_INVALID_ARGUMENT);
    q.flags = 0U;
    CHECK(BootEngine_Process(&engine, &q, &r) == 0);
    q = Request(BOOT_PROTOCOL_COMMAND_QUERY_SLOTS, 0U);
    ++q.sequence;
    CHECK(BootEngine_Process(&engine, &q, &r) == BOOT_PROTOCOL_STATUS_SEQUENCE);
    --q.sequence;
    CHECK(BootEngine_Process(&engine, &q, &r) == 0);

    q = Request(BOOT_PROTOCOL_COMMAND_ERASE_INACTIVE, 1U);
    CHECK(BootEngine_Process(&engine, &q, &r) == 0);
    m.fail_write = true;
    q = Request(BOOT_PROTOCOL_COMMAND_WRITE_CHUNK, 1U);
    q.payload_length = 3U;
    memset(q.payload, 0x5A, 3U);
    const uint32_t calls_before_error = m.write_calls;
    CHECK(BootEngine_Process(&engine, &q, &r) == BOOT_PROTOCOL_STATUS_FLASH);
    CHECK(BootEngine_Process(&engine, &q, &duplicate_response) == BOOT_PROTOCOL_STATUS_FLASH);
    CHECK(m.write_calls == calls_before_error + 1U);
    puts("PASS boot engine workflow");
    return 0;
}
