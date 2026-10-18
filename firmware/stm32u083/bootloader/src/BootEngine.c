#include "stm32u083_bootloader/BootEngine.h"
#include "firmware_image/FirmwareImage.h"
#include <string.h>
static bool BootEngine_RequestMatches(const BootProtocol_Message *a,
                                      const BootProtocol_Message *b) {
    return a->command == b->command && a->sequence == b->sequence && a->flags == b->flags &&
           a->operation_offset == b->operation_offset && a->payload_length == b->payload_length &&
           memcmp(a->payload, b->payload, a->payload_length) == 0;
}
static void BootEngine_Query(BootEngine_Context *c, BootProtocol_Message *r) {
    BootJournal_Snapshot s;
    uint8_t mask = 0U;
    for (uint8_t slot = 0U; slot < 2U; ++slot)
        if (c->storage.validate_slot(c->storage.context, slot, 0U))
            mask |= (uint8_t)(1U << slot);
    r->payload_length = BOOT_PROTOCOL_QUERY_RESPONSE_SIZE;
    memset(r->payload, 0, r->payload_length);
    r->payload[0] = mask;
    if (BootJournal_Load(&c->journal, &s) == BOOT_JOURNAL_OK) {
        r->payload[1] = s.record.confirmed_slot;
        r->payload[2] = s.record.selected_slot;
        r->payload[3] = (uint8_t)s.record.state;
        r->payload[5] = s.record.trial_attempts;
    } else {
        r->payload[1] = BOOT_PROTOCOL_NO_SLOT;
        r->payload[2] = BOOT_PROTOCOL_NO_SLOT;
        r->payload[3] = BOOT_JOURNAL_STATE_RECOVERY;
    }
    r->payload[4] = c->active_slot;
}
void BootEngine_Init(BootEngine_Context *c, const BootJournal_Flash *j,
                     const BootEngine_Storage *s) {
    memset(c, 0, sizeof(*c));
    c->journal = *j;
    c->storage = *s;
    c->update_slot = BOOT_PROTOCOL_NO_SLOT;
    c->active_slot = BOOT_PROTOCOL_NO_SLOT;
}
void BootEngine_SetActiveSlot(BootEngine_Context *c, uint8_t active_slot) {
    if (c != NULL)
        c->active_slot = active_slot < 2U ? active_slot : BOOT_PROTOCOL_NO_SLOT;
}
BootProtocol_Status BootEngine_Process(BootEngine_Context *c, const BootProtocol_Message *q,
                                       BootProtocol_Message *r) {
    if (!c || !q || !r || !c->storage.validate_slot ||
        q->payload_length > BOOT_PROTOCOL_MAX_PAYLOAD_SIZE)
        return BOOT_PROTOCOL_STATUS_INVALID_ARGUMENT;
    memset(r, 0, sizeof(*r));
    r->command = q->command;
    r->sequence = q->sequence;
    r->flags = (uint16_t)(BOOT_PROTOCOL_FLAG_RESPONSE | (q->flags & BOOT_PROTOCOL_FLAG_SLOT_MASK));
    if ((q->flags & (uint16_t)~BOOT_PROTOCOL_FLAG_SLOT_MASK) != 0U || q->status != 0U) {
        r->status = BOOT_PROTOCOL_STATUS_INVALID_ARGUMENT;
        return BOOT_PROTOCOL_STATUS_INVALID_ARGUMENT;
    }
    if (c->cached && q->sequence == c->cached_request.sequence) {
        if (BootEngine_RequestMatches(q, &c->cached_request)) {
            *r = c->cached_response;
            return (BootProtocol_Status)r->status;
        }
        r->status = BOOT_PROTOCOL_STATUS_SEQUENCE;
        return BOOT_PROTOCOL_STATUS_SEQUENCE;
    }
    if (c->sequence_initialized && q->sequence != c->expected_sequence) {
        r->status = BOOT_PROTOCOL_STATUS_SEQUENCE;
        return BOOT_PROTOCOL_STATUS_SEQUENCE;
    }
    uint8_t slot = (uint8_t)(q->flags & BOOT_PROTOCOL_FLAG_SLOT_MASK);
    BootProtocol_Status status = BOOT_PROTOCOL_STATUS_OK;
    BootJournal_Snapshot snapshot;
    switch ((BootProtocol_Command)q->command) {
    case BOOT_PROTOCOL_COMMAND_QUERY_SLOTS:
    case BOOT_PROTOCOL_COMMAND_GET_BOOT_STATE:
        if (q->payload_length != 0U)
            status = BOOT_PROTOCOL_STATUS_INVALID_ARGUMENT;
        else
            BootEngine_Query(c, r);
        break;
    case BOOT_PROTOCOL_COMMAND_ERASE_INACTIVE:
        if (q->payload_length != 0U || !c->storage.erase_slot)
            status = BOOT_PROTOCOL_STATUS_INVALID_ARGUMENT;
        else if (BootJournal_Load(&c->journal, &snapshot) == BOOT_JOURNAL_OK &&
                 snapshot.record.confirmed_slot == slot)
            status = BOOT_PROTOCOL_STATUS_STATE;
        else {
            status = c->storage.erase_slot(c->storage.context, slot);
            if (status == BOOT_PROTOCOL_STATUS_OK) {
                c->update_slot = slot;
                c->next_offset = 0U;
                c->erased = true;
                c->finalized = false;
            }
        }
        break;
    case BOOT_PROTOCOL_COMMAND_WRITE_CHUNK:
        if (!c->erased || c->update_slot != slot)
            status = BOOT_PROTOCOL_STATUS_STATE;
        else if (q->payload_length == 0U || q->operation_offset != c->next_offset ||
                 c->next_offset > FIRMWARE_IMAGE_STM32_SLOT_SIZE ||
                 q->payload_length > FIRMWARE_IMAGE_STM32_SLOT_SIZE - c->next_offset)
            status = BOOT_PROTOCOL_STATUS_BOUNDARY;
        else {
            status = c->storage.write(c->storage.context, slot, c->next_offset, q->payload,
                                      q->payload_length);
            if (status == BOOT_PROTOCOL_STATUS_OK)
                c->next_offset += q->payload_length;
        }
        break;
    case BOOT_PROTOCOL_COMMAND_FINALIZE:
        if (q->payload_length != 0U || !c->erased || c->update_slot != slot ||
            q->operation_offset != c->next_offset)
            status = BOOT_PROTOCOL_STATUS_SEQUENCE;
        else if (!c->storage.validate_slot(c->storage.context, slot, c->next_offset))
            status = BOOT_PROTOCOL_STATUS_SIGNATURE;
        else {
            c->finalized = true;
            c->finalized_size = c->next_offset;
        }
        break;
    case BOOT_PROTOCOL_COMMAND_SET_PENDING:
        if (q->payload_length != 0U || !c->finalized || c->update_slot != slot)
            status = BOOT_PROTOCOL_STATUS_STATE;
        else {
            BootJournal_Record n = {.generation = 0U,
                                    .state = BOOT_JOURNAL_STATE_PENDING,
                                    .confirmed_slot = BOOT_JOURNAL_SLOT_NONE,
                                    .selected_slot = slot,
                                    .trial_attempts = 0U};
            if (BootJournal_Load(&c->journal, &snapshot) == BOOT_JOURNAL_OK)
                n.confirmed_slot = snapshot.record.confirmed_slot;
            if (BootJournal_Append(&c->journal, &n, &snapshot) != BOOT_JOURNAL_OK)
                status = BOOT_PROTOCOL_STATUS_FLASH;
        }
        break;
    case BOOT_PROTOCOL_COMMAND_REBOOT:
        if (q->payload_length != 0U)
            status = BOOT_PROTOCOL_STATUS_INVALID_ARGUMENT;
        else if (c->storage.reboot)
            c->storage.reboot(c->storage.context);
        break;
    default:
        status = BOOT_PROTOCOL_STATUS_UNSUPPORTED;
        break;
    }
    r->status = (uint8_t)status;
    c->cached_request = *q;
    c->cached_response = *r;
    c->cached = true;
    c->sequence_initialized = true;
    c->expected_sequence = (uint16_t)(q->sequence + 1U);
    return status;
}
