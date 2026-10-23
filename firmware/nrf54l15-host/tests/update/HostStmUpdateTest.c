#include "HostStmUpdate.h"
#include "firmware_image/FirmwareImage.h"
#include "stm32u083_bootloader/BootEngine.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            printf("FAIL %d: %s\n", __LINE__, #x);                                                 \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

typedef struct {
    HostStmUpdate_Context host;
    BootEngine_Context engine;
    BootJournal_Flash journal;
    BootEngine_Storage storage;
    uint8_t metadata[2][2048];
    uint8_t image[520];
    uint8_t received[520];
    uint8_t rx[1024];
    size_t rx_length;
    size_t fragment;
    int64_t clock;
    unsigned resets, writes, erases;
    uint8_t drop_command;
    bool leave_pending, malformed_commit;
    bool valid[2], online, drop, rollback, invalid_signature, wrong_sequence, wrong_slot;
} Model;

static BootJournal_Result ReadJournal(void *p, uint8_t page, uint16_t offset, uint8_t *data,
                                      size_t n) {
    Model *m = p;
    memcpy(data, &m->metadata[page][offset], n);
    return BOOT_JOURNAL_OK;
}
static BootJournal_Result WriteJournal(void *p, uint8_t page, uint16_t offset, const uint8_t d[8]) {
    Model *m = p;
    for (size_t i = 0U; i < 8U; ++i)
        m->metadata[page][offset + i] &= d[i];
    return BOOT_JOURNAL_OK;
}
static BootJournal_Result EraseJournal(void *p, uint8_t page) {
    memset(((Model *)p)->metadata[page], 255, 2048U);
    return BOOT_JOURNAL_OK;
}
static BootProtocol_Status Erase(void *p, uint8_t slot) {
    Model *m = p;
    if (slot != 1U)
        return BOOT_PROTOCOL_STATUS_STATE;
    ++m->erases;
    m->valid[slot] = false;
    memset(m->received, 255, sizeof(m->received));
    return BOOT_PROTOCOL_STATUS_OK;
}
static BootProtocol_Status Write(void *p, uint8_t slot, uint32_t offset, const uint8_t *data,
                                 size_t n) {
    Model *m = p;
    if (slot != 1U || offset > sizeof(m->received) || n > sizeof(m->received) - offset)
        return BOOT_PROTOCOL_STATUS_BOUNDARY;
    ++m->writes;
    memcpy(&m->received[offset], data, n);
    return BOOT_PROTOCOL_STATUS_OK;
}
static bool Validate(void *p, uint8_t slot, uint32_t size) {
    Model *m = p;
    if (size == 0U)
        return m->valid[slot];
    if (m->invalid_signature)
        return false;
    return size == sizeof(m->received) && memcmp(m->image, m->received, size) == 0;
}
static int Send(void *p, const uint8_t *data, size_t n) {
    Model *m = p;
    BootProtocol_Message q, r;
    if (BootProtocol_Decode(data, n, &q) != BOOT_PROTOCOL_STATUS_OK)
        return -EPROTO;
    (void)BootEngine_Process(&m->engine, &q, &r);
    if (m->malformed_commit && q.command == BOOT_PROTOCOL_COMMAND_SET_PENDING) {
        r.payload_length = 1U;
        r.payload[0] = 0xAAU;
    }
    if (m->drop || q.command == m->drop_command)
        return 0;
    if (m->wrong_sequence)
        ++r.sequence;
    if (m->wrong_slot)
        r.flags ^= 1U;
    size_t length = 0U;
    if (BootProtocol_Encode(&r, &m->rx[m->rx_length], sizeof(m->rx) - m->rx_length, &length) != 0)
        return -ENOBUFS;
    m->rx_length += length;
    return 0;
}
static size_t Read(void *p, uint8_t *data, size_t capacity) {
    Model *m = p;
    size_t n = m->rx_length;
    if (n > capacity)
        n = capacity;
    if (n > m->fragment)
        n = m->fragment;
    memcpy(data, m->rx, n);
    m->rx_length -= n;
    memmove(m->rx, &m->rx[n], m->rx_length);
    return n;
}
static int64_t Now(void *p) { return ((Model *)p)->clock; }
static int Reset(void *p) {
    Model *m = p;
    ++m->resets;
    m->rx_length = 0U;
    m->online = false;
    BootEngine_Init(&m->engine, &m->journal, &m->storage);
    return 0;
}
static void Runtime(void *p) {
    Model *m = p;
    BootJournal_Snapshot s;
    if (!m->leave_pending && BootJournal_Load(&m->journal, &s) == 0 &&
        s.record.state == BOOT_JOURNAL_STATE_PENDING) {
        s.record.state = BOOT_JOURNAL_STATE_CONFIRMED;
        s.record.confirmed_slot = (uint8_t)(m->rollback ? 0U : 1U);
        s.record.selected_slot = s.record.confirmed_slot;
        m->valid[1] = !m->rollback;
        (void)BootJournal_Append(&m->journal, &s.record, &s);
    }
    m->online = true;
}
static bool Online(void *p) { return ((Model *)p)->online; }
static void U32(uint8_t *d, uint32_t v) {
    for (unsigned i = 0U; i < 4U; ++i)
        d[i] = (uint8_t)(v >> (8U * i));
}
static void Init(Model *m) {
    memset(m, 0, sizeof(*m));
    m->fragment = sizeof(m->rx);
    memset(m->metadata, 255, sizeof(m->metadata));
    m->valid[0] = true;
    m->journal = (BootJournal_Flash){m, ReadJournal, WriteJournal, EraseJournal};
    m->storage = (BootEngine_Storage){m, Erase, Write, Validate, NULL};
    BootJournal_Record r = {0U, BOOT_JOURNAL_STATE_CONFIRMED, 0U, 0U, 0U};
    BootJournal_Snapshot s;
    (void)BootJournal_Append(&m->journal, &r, &s);
    HostStmUpdate_Ops ops = {m, Send, Read, Now, Reset, Runtime, Online};
    HostStmUpdate_Initialize(&m->host, &ops);
    memcpy(m->image, "MMFW", 4U);
    m->image[4] = 2U;
    m->image[7] = 2U;
    U32(&m->image[8], FIRMWARE_IMAGE_TARGET_STM32U083_APPLICATION);
    U32(&m->image[12], FIRMWARE_IMAGE_HARDWARE_NUCLEO_U083RC);
    m->image[22] = 1U;
    U32(&m->image[28], FIRMWARE_IMAGE_STM32_SLOT1_VECTOR_ADDRESS);
    U32(&m->image[32], 8U);
    m->image[36] = 1U;
    m->image[38] = 1U;
    m->image[42] = 1U;
    m->image[43] = 1U;
    m->image[111] = 1U; /* canonical, nonzero P256 r/s integers */
    m->image[143] = 1U;
}
static void Pump(Model *m) {
    for (unsigned i = 0U; i < 80U; ++i)
        HostStmUpdate_Process(&m->host);
}
static int Stream(Model *m) {
    CHECK(HostStmUpdate_Begin(&m->host, m->image, sizeof(m->image)) == 0);
    Pump(m);
    CHECK(m->host.status.state == HOST_STM_UPDATE_READY);
    CHECK(m->host.status.offset == 0U && m->erases == 1U);
    CHECK(HostStmUpdate_Write(&m->host, 1U, m->image, 8U) == -EINVAL);
    uint8_t wrong[256];
    memcpy(wrong, m->image, sizeof(wrong));
    wrong[99] ^= 1U;
    CHECK(HostStmUpdate_Write(&m->host, 0U, wrong, sizeof(wrong)) == -EBADMSG);
    for (uint32_t offset = 0U; offset < sizeof(m->image);) {
        size_t n = sizeof(m->image) - offset;
        if (n > 256U)
            n = 256U;
        CHECK(HostStmUpdate_Write(&m->host, offset, &m->image[offset], n) == 0);
        CHECK(m->host.status.offset == offset);
        CHECK(HostStmUpdate_Write(&m->host, offset, &m->image[offset], n) == 0);
        Pump(m);
        CHECK(m->host.status.offset == offset + n);
        CHECK(HostStmUpdate_Write(&m->host, offset, &m->image[offset], n) == 0);
        offset += (uint32_t)n;
    }
    CHECK(m->writes == 3U && m->host.status.offset == sizeof(m->image));
    return 0;
}
int main(void) {
    Model m;
    Init(&m);
    m.fragment = 7U;
    CHECK(Stream(&m) == 0);
    CHECK(HostStmUpdate_Finish(&m.host) == 0);
    Pump(&m);
    CHECK(m.host.status.state == HOST_STM_UPDATE_COMPLETE);
    CHECK(m.host.status.confirmed_slot == 1U && !m.host.status.active);
    CHECK(m.resets == 4U && !HostStmUpdate_UsesBootTransport(&m.host));

    Init(&m);
    m.rollback = true;
    CHECK(Stream(&m) == 0);
    CHECK(HostStmUpdate_Finish(&m.host) == 0);
    Pump(&m);
    CHECK(m.host.status.state == HOST_STM_UPDATE_ROLLED_BACK);

    Init(&m);
    CHECK(Stream(&m) == 0);
    m.invalid_signature = true;
    CHECK(HostStmUpdate_Finish(&m.host) == 0);
    Pump(&m);
    CHECK(m.host.status.state == HOST_STM_UPDATE_FAILED);
    CHECK(m.host.status.protocol == BOOT_PROTOCOL_STATUS_SIGNATURE && !m.host.committed);
    CHECK(m.valid[0]);

    Init(&m);
    CHECK(Stream(&m) == 0);
    m.drop_command = BOOT_PROTOCOL_COMMAND_SET_PENDING;
    CHECK(HostStmUpdate_Finish(&m.host) == 0);
    Pump(&m);
    CHECK(m.host.status.state == HOST_STM_UPDATE_PENDING && m.host.committed);
    CHECK(HostStmUpdate_Abort(&m.host) == -EPERM);
    for (unsigned i = 0U; i < 4U; ++i) {
        m.clock += 5000;
        Pump(&m);
    }
    CHECK(m.host.status.state == HOST_STM_UPDATE_COMPLETE && m.host.status.confirmed_slot == 1U);

    Init(&m);
    CHECK(Stream(&m) == 0);
    m.malformed_commit = true;
    CHECK(HostStmUpdate_Finish(&m.host) == 0);
    Pump(&m);
    CHECK(m.host.status.state == HOST_STM_UPDATE_COMPLETE && m.host.status.confirmed_slot == 1U);
    CHECK(m.resets == 4U);

    Init(&m);
    CHECK(Stream(&m) == 0);
    m.leave_pending = true;
    CHECK(HostStmUpdate_Finish(&m.host) == 0);
    Pump(&m);
    CHECK(m.host.status.state == HOST_STM_UPDATE_FAILED);
    CHECK(m.host.status.protocol == BOOT_PROTOCOL_STATUS_STATE);

    Init(&m);
    CHECK(HostStmUpdate_Begin(&m.host, m.image, sizeof(m.image)) == 0);
    /* Corrupt response CRC, precede it with noise, then recover through a retry. */
    m.rx[19] ^= 0x40U;
    memmove(&m.rx[3], m.rx, m.rx_length);
    memset(m.rx, 0xAA, 3U);
    m.rx_length += 3U;
    Pump(&m);
    CHECK(m.erases == 0U);
    m.clock += 250;
    Pump(&m);
    CHECK(m.erases == 1U && m.host.status.state == HOST_STM_UPDATE_READY);
    m.wrong_slot = true;
    CHECK(HostStmUpdate_Write(&m.host, 0U, m.image, 256U) == 0);
    Pump(&m);
    CHECK(m.host.status.offset == 0U);
    m.wrong_slot = false;
    m.clock += 5000;
    Pump(&m);
    CHECK(m.host.status.offset == 256U && m.writes == 1U);

    Init(&m);
    CHECK(HostStmUpdate_Begin(&m.host, m.image, sizeof(m.image)) == 0);
    Pump(&m);
    m.drop = true;
    CHECK(HostStmUpdate_Write(&m.host, 0U, m.image, 256U) == 0);
    Pump(&m);
    CHECK(m.host.status.offset == 0U && m.writes == 1U);
    m.drop = false;
    m.clock += 5000;
    Pump(&m);
    CHECK(m.host.status.offset == 256U && m.writes == 1U);
    m.clock += 30000;
    Pump(&m);
    CHECK(m.host.status.state == HOST_STM_UPDATE_ABORTED && m.valid[0]);

    Init(&m);
    BootJournal_Record pending = {0U, BOOT_JOURNAL_STATE_PENDING, 1U, 0U, 0U};
    BootJournal_Snapshot snapshot;
    CHECK(BootJournal_Append(&m.journal, &pending, &snapshot) == 0);
    CHECK(HostStmUpdate_Begin(&m.host, m.image, sizeof(m.image)) == 0);
    Pump(&m);
    CHECK(m.erases == 0U && m.host.status.state == HOST_STM_UPDATE_FAILED);

    Init(&m);
    m.wrong_sequence = true;
    CHECK(HostStmUpdate_Begin(&m.host, m.image, sizeof(m.image)) == 0);
    Pump(&m);
    CHECK(m.erases == 0U);
    for (unsigned i = 0U; i < 60U; ++i) {
        m.clock += 250;
        Pump(&m);
    }
    CHECK(m.host.status.state == HOST_STM_UPDATE_FAILED && m.host.status.error == -ETIMEDOUT);

    Init(&m);
    CHECK(HostStmUpdate_Reset(&m.host) == 0);
    CHECK(HostStmUpdate_Reset(&m.host) == -EBUSY);
    Pump(&m);
    CHECK(m.resets == 1U && m.host.status.state == HOST_STM_UPDATE_COMPLETE);

    Init(&m);
    CHECK(HostStmUpdate_Begin(&m.host, m.image, sizeof(m.image)) == 0);
    CHECK(HostStmUpdate_Begin(&m.host, m.image, sizeof(m.image)) == -EBUSY);
    Pump(&m);
    CHECK(HostStmUpdate_Finish(&m.host) == -EINVAL);
    /* Arbitrary chunk boundaries must also compare the header intersection. */
    CHECK(HostStmUpdate_Write(&m.host, 0U, m.image, 63U) == 0);
    Pump(&m);
    uint8_t wrong_header[256];
    memcpy(wrong_header, &m.image[63], sizeof(wrong_header));
    wrong_header[200] ^= 1U;
    CHECK(HostStmUpdate_Write(&m.host, 63U, wrong_header, sizeof(wrong_header)) == -EBADMSG);
    CHECK(HostStmUpdate_Write(&m.host, 63U, &m.image[63], 256U) == 0);
    Pump(&m);
    CHECK(HostStmUpdate_Write(&m.host, 319U, &m.image[319], 201U) == 0);
    Pump(&m);
    CHECK(HostStmUpdate_Finish(&m.host) == 0);
    Pump(&m);
    CHECK(m.host.status.state == HOST_STM_UPDATE_COMPLETE && m.writes == 3U);

    Init(&m);
    m.image[36] = 9U;
    CHECK(HostStmUpdate_Begin(&m.host, m.image, sizeof(m.image)) == -EXDEV);
    CHECK(m.resets == 0U && m.erases == 0U);
    puts("PASS host STM streaming core with production boot engine/journal");
    return 0;
}
