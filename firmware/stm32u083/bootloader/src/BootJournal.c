#include "stm32u083_bootloader/BootJournal.h"
#include <string.h>
#define BOOT_JOURNAL_MAGIC 0x4A544F42UL
#define BOOT_JOURNAL_SCHEMA 1U
#define BOOT_JOURNAL_CRC_OFFSET 56U
static uint16_t BootJournal_ReadLe16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8U));
}
static uint32_t BootJournal_ReadLe32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) | ((uint32_t)p[2] << 16U) |
           ((uint32_t)p[3] << 24U);
}
static void BootJournal_WriteLe16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8U);
}
static void BootJournal_WriteLe32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8U);
    p[2] = (uint8_t)(v >> 16U);
    p[3] = (uint8_t)(v >> 24U);
}
static uint32_t BootJournal_Crc32(const uint8_t *p, size_t n) {
    uint32_t c = 0xFFFFFFFFUL;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (uint8_t b = 0; b < 8U; b++) {
            uint32_t m = 0U - (c & 1U);
            c = (c >> 1U) ^ (0xEDB88320UL & m);
        }
    }
    return ~c;
}
static bool BootJournal_StateValid(uint8_t s) {
    return s >= BOOT_JOURNAL_STATE_CONFIRMED && s <= BOOT_JOURNAL_STATE_RECOVERY;
}
static bool BootJournal_GenerationNewer(uint32_t a, uint32_t b) {
    return a != b && (uint32_t)(a - b) < 0x80000000UL;
}
static void BootJournal_Encode(const BootJournal_Record *r, uint8_t raw[BOOT_JOURNAL_RECORD_SIZE]) {
    memset(raw, 0xFF, BOOT_JOURNAL_RECORD_SIZE);
    BootJournal_WriteLe32(raw, BOOT_JOURNAL_MAGIC);
    BootJournal_WriteLe16(raw + 4, BOOT_JOURNAL_SCHEMA);
    raw[6] = (uint8_t)r->state;
    raw[7] = 0U;
    BootJournal_WriteLe32(raw + 8, r->generation);
    raw[12] = r->confirmed_slot;
    raw[13] = r->selected_slot;
    raw[14] = r->trial_attempts;
    raw[15] = 0U;
    uint32_t c = BootJournal_Crc32(raw, BOOT_JOURNAL_CRC_OFFSET);
    BootJournal_WriteLe32(raw + BOOT_JOURNAL_CRC_OFFSET, c);
    BootJournal_WriteLe32(raw + BOOT_JOURNAL_CRC_OFFSET + 4U, ~c);
}
static bool BootJournal_Decode(const uint8_t raw[BOOT_JOURNAL_RECORD_SIZE], BootJournal_Record *r) {
    uint32_t c = BootJournal_ReadLe32(raw + BOOT_JOURNAL_CRC_OFFSET);
    if (BootJournal_ReadLe32(raw) != BOOT_JOURNAL_MAGIC ||
        BootJournal_ReadLe16(raw + 4) != BOOT_JOURNAL_SCHEMA || !BootJournal_StateValid(raw[6]) ||
        BootJournal_ReadLe32(raw + BOOT_JOURNAL_CRC_OFFSET + 4U) != ~c ||
        BootJournal_Crc32(raw, BOOT_JOURNAL_CRC_OFFSET) != c)
        return false;
    r->state = (BootJournal_State)raw[6];
    r->generation = BootJournal_ReadLe32(raw + 8);
    r->confirmed_slot = raw[12];
    r->selected_slot = raw[13];
    r->trial_attempts = raw[14];
    return true;
}
BootJournal_Result BootJournal_Load(const BootJournal_Flash *f, BootJournal_Snapshot *s) {
    if (!f || !f->read || !s)
        return BOOT_JOURNAL_ERROR_ARGUMENT;
    memset(s, 0, sizeof(*s));
    uint8_t raw[BOOT_JOURNAL_RECORD_SIZE];
    for (uint8_t p = 0; p < 2U; p++)
        for (uint8_t i = 0; i < BOOT_JOURNAL_RECORDS_PER_PAGE; i++) {
            BootJournal_Result x =
                f->read(f->context, p, (uint16_t)((uint16_t)i * BOOT_JOURNAL_RECORD_SIZE), raw,
                        sizeof(raw));
            if (x != BOOT_JOURNAL_OK)
                return x;
            BootJournal_Record r;
            if (BootJournal_Decode(raw, &r) &&
                (!s->found || BootJournal_GenerationNewer(r.generation, s->record.generation))) {
                s->found = true;
                s->page = p;
                s->record_index = i;
                s->record = r;
            }
        }
    return s->found ? BOOT_JOURNAL_OK : BOOT_JOURNAL_ERROR_NO_RECORD;
}
static BootJournal_Result BootJournal_FindFree(const BootJournal_Flash *f, uint8_t p,
                                               uint8_t *out) {
    uint8_t raw[BOOT_JOURNAL_RECORD_SIZE];
    for (uint8_t i = 0; i < BOOT_JOURNAL_RECORDS_PER_PAGE; i++) {
        BootJournal_Result x = f->read(
            f->context, p, (uint16_t)((uint16_t)i * BOOT_JOURNAL_RECORD_SIZE), raw, sizeof(raw));
        if (x != BOOT_JOURNAL_OK)
            return x;
        bool blank = true;
        for (size_t j = 0; j < sizeof(raw); j++)
            blank = blank && raw[j] == 0xFFU;
        if (blank) {
            *out = i;
            return BOOT_JOURNAL_OK;
        }
    }
    return BOOT_JOURNAL_ERROR_NO_RECORD;
}
BootJournal_Result BootJournal_Append(const BootJournal_Flash *f, const BootJournal_Record *r,
                                      BootJournal_Snapshot *s) {
    if (!f || !f->read || !f->program_doubleword || !f->erase_page || !r || !s ||
        !BootJournal_StateValid((uint8_t)r->state))
        return BOOT_JOURNAL_ERROR_ARGUMENT;
    BootJournal_Snapshot cur;
    BootJournal_Result load = BootJournal_Load(f, &cur);
    if (load != BOOT_JOURNAL_OK && load != BOOT_JOURNAL_ERROR_NO_RECORD) {
        return load;
    }
    uint8_t p = cur.found ? cur.page : 0U, i = 0U;
    BootJournal_Result x = BootJournal_FindFree(f, p, &i);
    if (x == BOOT_JOURNAL_ERROR_NO_RECORD) {
        p ^= 1U;
        x = f->erase_page(f->context, p);
        if (x != BOOT_JOURNAL_OK)
            return x;
        i = 0U;
    } else if (x != BOOT_JOURNAL_OK)
        return x;
    BootJournal_Record next = *r;
    next.generation = cur.found ? cur.record.generation + 1U : r->generation;
    uint8_t raw[64];
    BootJournal_Encode(&next, raw);
    uint16_t base = (uint16_t)((uint16_t)i * 64U);
    for (uint16_t o = 0; o < BOOT_JOURNAL_CRC_OFFSET; o += 8U) {
        x = f->program_doubleword(f->context, p, (uint16_t)(base + o), raw + o);
        if (x != BOOT_JOURNAL_OK)
            return x;
    }
    x = f->program_doubleword(f->context, p, (uint16_t)(base + BOOT_JOURNAL_CRC_OFFSET),
                              raw + BOOT_JOURNAL_CRC_OFFSET);
    return x == BOOT_JOURNAL_OK ? BootJournal_Load(f, s) : x;
}
static bool BootJournal_SlotValid(uint8_t slot, uint8_t mask) {
    return slot < 2U && (mask & (uint8_t)(1U << slot)) != 0U;
}
BootJournal_Result BootJournal_PrepareBoot(const BootJournal_Flash *f, uint8_t mask, uint8_t max,
                                           BootJournal_BootDecision *d) {
    if (!d || max == 0U)
        return BOOT_JOURNAL_ERROR_ARGUMENT;
    memset(d, 0, sizeof(*d));
    d->slot = BOOT_JOURNAL_SLOT_NONE;
    d->recovery = true;
    BootJournal_Snapshot cur;
    BootJournal_Result x = BootJournal_Load(f, &cur);
    if (x == BOOT_JOURNAL_ERROR_NO_RECORD)
        return BOOT_JOURNAL_OK;
    if (x != BOOT_JOURNAL_OK)
        return x;
    BootJournal_Record n = cur.record;
    uint8_t chosen = BOOT_JOURNAL_SLOT_NONE;
    bool save = false;
    switch (cur.record.state) {
    case BOOT_JOURNAL_STATE_CONFIRMED:
        chosen = cur.record.confirmed_slot;
        break;
    case BOOT_JOURNAL_STATE_PENDING:
        if (BootJournal_SlotValid(cur.record.selected_slot, mask)) {
            chosen = cur.record.selected_slot;
            n.state = BOOT_JOURNAL_STATE_TRIAL;
            n.trial_attempts = 1U;
            save = true;
        } else {
            chosen = cur.record.confirmed_slot;
            n.state = BOOT_JOURNAL_STATE_INVALID;
            n.trial_attempts = 0U;
            save = true;
        }
        break;
    case BOOT_JOURNAL_STATE_TRIAL:
        if (BootJournal_SlotValid(cur.record.selected_slot, mask) &&
            cur.record.trial_attempts < max) {
            chosen = cur.record.selected_slot;
            n.trial_attempts++;
            save = true;
        } else {
            chosen = cur.record.confirmed_slot;
            n.state = BOOT_JOURNAL_STATE_CONFIRMED;
            n.selected_slot = n.confirmed_slot;
            n.trial_attempts = 0U;
            save = true;
        }
        break;
    case BOOT_JOURNAL_STATE_INVALID:
        chosen = cur.record.confirmed_slot;
        break;
    case BOOT_JOURNAL_STATE_RECOVERY:
    default:
        break;
    }
    if (!BootJournal_SlotValid(chosen, mask))
        chosen = BOOT_JOURNAL_SLOT_NONE;
    if (save) {
        BootJournal_Snapshot written;
        x = BootJournal_Append(f, &n, &written);
        if (x != BOOT_JOURNAL_OK)
            return x;
        d->persisted_record = written.record;
    } else
        d->persisted_record = cur.record;
    d->slot = chosen;
    d->recovery = chosen == BOOT_JOURNAL_SLOT_NONE;
    return BOOT_JOURNAL_OK;
}
BootJournal_Result BootJournal_Confirm(const BootJournal_Flash *f, uint8_t slot,
                                       BootJournal_Snapshot *s) {
    if (slot >= 2U)
        return BOOT_JOURNAL_ERROR_ARGUMENT;
    BootJournal_Snapshot cur;
    BootJournal_Result x = BootJournal_Load(f, &cur);
    if (x != BOOT_JOURNAL_OK)
        return x;
    if (cur.record.state != BOOT_JOURNAL_STATE_TRIAL || cur.record.selected_slot != slot)
        return BOOT_JOURNAL_ERROR_ARGUMENT;
    BootJournal_Record n = cur.record;
    n.state = BOOT_JOURNAL_STATE_CONFIRMED;
    n.confirmed_slot = slot;
    n.selected_slot = slot;
    n.trial_attempts = 0U;
    return BootJournal_Append(f, &n, s);
}
