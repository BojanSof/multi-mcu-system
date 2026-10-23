#include "HostStmUpdate.h"
#include "firmware_image/FirmwareImage.h"

#include <errno.h>
#include <string.h>

#define HOST_STM_QUERY_MS 250
#define HOST_STM_RESPONSE_MS 5000
#define HOST_STM_INACTIVITY_MS 30000
#define HOST_STM_JOURNAL_CONFIRMED 1U

static int64_t HostStmUpdate_Now(HostStmUpdate_Context *c) { return c->ops.now(c->ops.context); }

static int HostStmUpdate_BootRuntime(HostStmUpdate_Context *c) {
    c->outstanding = false;
    const int result = c->ops.reset(c->ops.context);
    c->rx_length = 0U;
    c->boot_owned = false;
    c->ops.runtime_restart(c->ops.context);
    c->status.state = HOST_STM_UPDATE_BOOTING;
    c->deadline = HostStmUpdate_Now(c) + HOST_STM_INACTIVITY_MS;
    return result;
}

static void HostStmUpdate_Return(HostStmUpdate_Context *c, HostStmUpdate_State outcome) {
    c->outstanding = false;
    c->outcome = outcome;
    const int result = c->ops.reset(c->ops.context);
    c->rx_length = 0U;
    c->boot_owned = false;
    c->ops.runtime_restart(c->ops.context);
    c->status.state = HOST_STM_UPDATE_RETURNING;
    c->deadline = HostStmUpdate_Now(c) + HOST_STM_INACTIVITY_MS;
    if (result != 0) {
        c->status.error = result;
        c->outcome = HOST_STM_UPDATE_FAILED;
    }
}

static void HostStmUpdate_Fail(HostStmUpdate_Context *c, int error, uint8_t protocol) {
    /* A missing or malformed commit ACK cannot establish whether metadata was written.
     * Observe the journal after a trial boot instead of abandoning that job. */
    if (c->committed && c->status.state == HOST_STM_UPDATE_PENDING) {
        int result = HostStmUpdate_BootRuntime(c);
        if (result == 0) {
            return;
        }
        error = result;
    }
    c->status.error = error;
    c->status.protocol = protocol;
    HostStmUpdate_Return(c, HOST_STM_UPDATE_FAILED);
}

static int HostStmUpdate_Send(HostStmUpdate_Context *c, uint8_t command, uint32_t offset,
                              const uint8_t *data, uint16_t length, HostStmUpdate_State state) {
    BootProtocol_Message q = {0};
    q.command = command;
    q.sequence = c->sequence++;
    q.operation_offset = offset;
    q.payload_length = length;
    if (command != BOOT_PROTOCOL_COMMAND_QUERY_SLOTS) {
        q.flags = c->status.slot;
    }
    if (length != 0U) {
        memcpy(q.payload, data, length);
    }
    if (BootProtocol_Encode(&q, c->tx, sizeof(c->tx), &c->tx_length) != BOOT_PROTOCOL_STATUS_OK) {
        return -EINVAL;
    }
    c->status.state = state;
    int result = c->ops.send(c->ops.context, c->tx, c->tx_length);
    if (result != 0) {
        return result;
    }
    c->pending_command = command;
    c->pending_sequence = q.sequence;
    c->pending_length = length;
    c->outstanding = true;
    c->retries = 0U;
    c->deadline =
        HostStmUpdate_Now(c) +
        (command == BOOT_PROTOCOL_COMMAND_QUERY_SLOTS ? HOST_STM_QUERY_MS : HOST_STM_RESPONSE_MS);
    c->status.state = state;
    return 0;
}

static int HostStmUpdate_Enter(HostStmUpdate_Context *c, HostStmUpdate_State state) {
    c->boot_owned = true;
    c->rx_length = 0U;
    c->sequence = 1U;
    c->outstanding = false;
    int result = c->ops.reset(c->ops.context);
    if (result == 0) {
        result = HostStmUpdate_Send(c, BOOT_PROTOCOL_COMMAND_QUERY_SLOTS, 0U, NULL, 0U, state);
    }
    if (result != 0) {
        HostStmUpdate_Fail(c, result, BOOT_PROTOCOL_STATUS_BUSY);
    }
    return result;
}

void HostStmUpdate_Initialize(HostStmUpdate_Context *c, const HostStmUpdate_Ops *ops) {
    memset(c, 0, sizeof(*c));
    c->ops = *ops;
    c->status.slot = BOOT_PROTOCOL_NO_SLOT;
    c->status.confirmed_slot = BOOT_PROTOCOL_NO_SLOT;
}

int HostStmUpdate_Begin(HostStmUpdate_Context *c, const uint8_t header[512], uint32_t total) {
    FirmwareImage_SignedManifest manifest;
    if (c->status.active) {
        return -EBUSY;
    }
    if (FirmwareImage_ParseSignedPrefix(header, 512U, total, &manifest) !=
        FIRMWARE_IMAGE_STATUS_OK) {
        return -EBADMSG;
    }
    const FirmwareImage_Manifest *m = &manifest.manifest;
    const uint32_t vector = manifest.slot_id == 0U ? FIRMWARE_IMAGE_STM32_SLOT0_VECTOR_ADDRESS
                                                   : FIRMWARE_IMAGE_STM32_SLOT1_VECTOR_ADDRESS;
    if (m->target_id != FIRMWARE_IMAGE_TARGET_STM32U083_APPLICATION ||
        m->hardware_id != FIRMWARE_IMAGE_HARDWARE_NUCLEO_U083RC || m->load_address != vector ||
        m->payload_size > FIRMWARE_IMAGE_STM32_MAX_PAYLOAD_SIZE || m->payload_size == 0U ||
        m->required_wire_major != 1U || m->required_wire_minor != 0U ||
        m->required_management_major != 1U || m->required_management_minor != 0U) {
        return -EXDEV;
    }
    HostStmUpdate_Ops ops = c->ops;
    HostStmUpdate_Initialize(c, &ops);
    memcpy(c->header, header, sizeof(c->header));
    c->status.total = total;
    c->status.slot = manifest.slot_id;
    c->status.active = true;
    c->activity = HostStmUpdate_Now(c);
    return HostStmUpdate_Enter(c, HOST_STM_UPDATE_QUERYING);
}

int HostStmUpdate_Reset(HostStmUpdate_Context *c) {
    if (c->status.active) {
        return -EBUSY;
    }
    HostStmUpdate_Ops ops = c->ops;
    HostStmUpdate_Initialize(c, &ops);
    c->status.active = true;
    c->reset_only = true;
    HostStmUpdate_Return(c, HOST_STM_UPDATE_COMPLETE);
    return c->status.error;
}

int HostStmUpdate_Write(HostStmUpdate_Context *c, uint32_t offset, const uint8_t *data,
                        size_t length) {
    if (data == NULL || length == 0U || length > BOOT_PROTOCOL_MAX_PAYLOAD_SIZE) {
        return -EINVAL;
    }
    /* The encoded last request is retained after ACK, allowing an exact retry
     * without advancing offset or programming a flash word twice. */
    BootProtocol_Message previous;
    if (c->pending_command == BOOT_PROTOCOL_COMMAND_WRITE_CHUNK &&
        BootProtocol_Decode(c->tx, c->tx_length, &previous) == BOOT_PROTOCOL_STATUS_OK &&
        previous.operation_offset == offset && previous.payload_length == length &&
        memcmp(previous.payload, data, length) == 0 && c->status.active &&
        (c->status.state == HOST_STM_UPDATE_READY || c->status.state == HOST_STM_UPDATE_WRITING)) {
        c->activity = HostStmUpdate_Now(c);
        return 0;
    }
    if (c->status.state != HOST_STM_UPDATE_READY || c->outstanding || offset != c->status.offset ||
        offset > c->status.total || length > c->status.total - offset) {
        return -EINVAL;
    }
    if (offset < sizeof(c->header)) {
        size_t n = sizeof(c->header) - offset;
        if (n > length) {
            n = length;
        }
        if (memcmp(data, &c->header[offset], n) != 0) {
            return -EBADMSG;
        }
    }
    c->activity = HostStmUpdate_Now(c);
    int result = HostStmUpdate_Send(c, BOOT_PROTOCOL_COMMAND_WRITE_CHUNK, offset, data,
                                    (uint16_t)length, HOST_STM_UPDATE_WRITING);
    if (result != 0) {
        HostStmUpdate_Fail(c, result, BOOT_PROTOCOL_STATUS_BUSY);
    }
    return result;
}

int HostStmUpdate_Finish(HostStmUpdate_Context *c) {
    if (c->status.state != HOST_STM_UPDATE_READY || c->outstanding ||
        c->status.offset != c->status.total) {
        return -EINVAL;
    }
    c->activity = HostStmUpdate_Now(c);
    int result = HostStmUpdate_Send(c, BOOT_PROTOCOL_COMMAND_FINALIZE, c->status.total, NULL, 0U,
                                    HOST_STM_UPDATE_FINALIZING);
    if (result != 0) {
        HostStmUpdate_Fail(c, result, BOOT_PROTOCOL_STATUS_BUSY);
    }
    return result;
}

int HostStmUpdate_Abort(HostStmUpdate_Context *c) {
    if (!c->status.active || c->committed || c->status.state == HOST_STM_UPDATE_RETURNING) {
        return -EPERM;
    }
    HostStmUpdate_Return(c, HOST_STM_UPDATE_ABORTED);
    return 0;
}

static bool HostStmUpdate_ValidSlot(uint8_t slot) {
    return slot < 2U || slot == BOOT_PROTOCOL_NO_SLOT;
}

static void HostStmUpdate_Response(HostStmUpdate_Context *c, const BootProtocol_Message *r) {
    if (!c->outstanding || r->command != c->pending_command || r->sequence != c->pending_sequence ||
        r->flags !=
            (uint16_t)(BOOT_PROTOCOL_FLAG_RESPONSE |
                       (r->command == BOOT_PROTOCOL_COMMAND_QUERY_SLOTS ? 0U : c->status.slot))) {
        return;
    }
    c->outstanding = false;
    c->status.protocol = r->status;
    if (r->status != BOOT_PROTOCOL_STATUS_OK) {
        HostStmUpdate_Fail(c, -EIO, r->status);
        return;
    }
    int result = 0;
    switch (c->status.state) {
    case HOST_STM_UPDATE_QUERYING:
    case HOST_STM_UPDATE_VERIFYING: {
        if (r->payload_length != BOOT_PROTOCOL_QUERY_RESPONSE_SIZE ||
            !HostStmUpdate_ValidSlot(r->payload[1]) || !HostStmUpdate_ValidSlot(r->payload[2]) ||
            !HostStmUpdate_ValidSlot(r->payload[4]) || r->payload[3] < 1U || r->payload[3] > 5U ||
            (r->payload[0] & 0xFCU) != 0U) {
            HostStmUpdate_Fail(c, -EPROTO, BOOT_PROTOCOL_STATUS_BAD_FRAME);
            return;
        }
        c->status.confirmed_slot = r->payload[1];
        c->status.boot_state = r->payload[3];
        if (c->status.state == HOST_STM_UPDATE_VERIFYING) {
            if (r->payload[3] != HOST_STM_JOURNAL_CONFIRMED || r->payload[1] != r->payload[2] ||
                r->payload[1] > 1U || (r->payload[0] & (1U << r->payload[1])) == 0U) {
                HostStmUpdate_Fail(c, -EPROTO, BOOT_PROTOCOL_STATUS_STATE);
            } else if (r->payload[1] == c->status.slot) {
                HostStmUpdate_Return(c, HOST_STM_UPDATE_COMPLETE);
            } else if (r->payload[1] == c->previous_confirmed_slot) {
                HostStmUpdate_Return(c, HOST_STM_UPDATE_ROLLED_BACK);
            } else {
                HostStmUpdate_Fail(c, -EPROTO, BOOT_PROTOCOL_STATUS_STATE);
            }
            return;
        }
        c->previous_confirmed_slot = r->payload[1];
        if (c->status.slot == r->payload[1] || c->status.slot == r->payload[2] ||
            c->status.slot == r->payload[4]) {
            HostStmUpdate_Fail(c, -EXDEV, BOOT_PROTOCOL_STATUS_STATE);
            return;
        }
        result = HostStmUpdate_Send(c, BOOT_PROTOCOL_COMMAND_ERASE_INACTIVE, c->status.total, NULL,
                                    0U, HOST_STM_UPDATE_ERASING);
        break;
    }
    case HOST_STM_UPDATE_ERASING:
    case HOST_STM_UPDATE_WRITING:
        if (r->payload_length != 0U) {
            HostStmUpdate_Fail(c, -EPROTO, BOOT_PROTOCOL_STATUS_BAD_FRAME);
            return;
        }
        if (c->status.state == HOST_STM_UPDATE_WRITING) {
            c->status.offset += c->pending_length;
        }
        c->status.state = HOST_STM_UPDATE_READY;
        c->activity = HostStmUpdate_Now(c);
        break;
    case HOST_STM_UPDATE_FINALIZING:
        if (r->payload_length != 0U) {
            HostStmUpdate_Fail(c, -EPROTO, BOOT_PROTOCOL_STATUS_BAD_FRAME);
            return;
        }
        /* Once SET_PENDING may be on the wire cancellation is unsafe, including
         * when its reply is lost. Continue verification autonomously. */
        c->committed = true;
        result = HostStmUpdate_Send(c, BOOT_PROTOCOL_COMMAND_SET_PENDING, 0U, NULL, 0U,
                                    HOST_STM_UPDATE_PENDING);
        break;
    case HOST_STM_UPDATE_PENDING:
        if (r->payload_length != 0U) {
            HostStmUpdate_Fail(c, -EPROTO, BOOT_PROTOCOL_STATUS_BAD_FRAME);
            return;
        }
        result = HostStmUpdate_BootRuntime(c);
        break;
    default:
        HostStmUpdate_Fail(c, -EPROTO, BOOT_PROTOCOL_STATUS_STATE);
        return;
    }
    if (result != 0) {
        HostStmUpdate_Fail(c, result, BOOT_PROTOCOL_STATUS_BUSY);
    }
}

static void HostStmUpdate_Drain(HostStmUpdate_Context *c) {
    size_t available = sizeof(c->rx) - c->rx_length;
    size_t got = c->ops.read(c->ops.context, &c->rx[c->rx_length], available);
    if (got > available) {
        HostStmUpdate_Fail(c, -EOVERFLOW, BOOT_PROTOCOL_STATUS_BAD_FRAME);
        return;
    }
    c->rx_length += got;
    while (c->boot_owned && c->rx_length >= BOOT_PROTOCOL_HEADER_SIZE) {
        uint16_t length = (uint16_t)((uint16_t)c->rx[8] | ((uint16_t)c->rx[9] << 8U));
        if (c->rx[0] != BOOT_PROTOCOL_MAGIC_0 || c->rx[1] != BOOT_PROTOCOL_MAGIC_1 ||
            length > BOOT_PROTOCOL_MAX_PAYLOAD_SIZE) {
            --c->rx_length;
            memmove(c->rx, &c->rx[1], c->rx_length);
            continue;
        }
        size_t frame_length = BOOT_PROTOCOL_HEADER_SIZE + length;
        if (c->rx_length < frame_length) {
            break;
        }
        BootProtocol_Message response;
        BootProtocol_Status status = BootProtocol_Decode(c->rx, frame_length, &response);
        /* On bad CRC advance one byte, not the claimed frame length: it may
         * contain a valid response after corrupt/noisy header bytes. */
        size_t consume = status == BOOT_PROTOCOL_STATUS_OK ? frame_length : 1U;
        c->rx_length -= consume;
        memmove(c->rx, &c->rx[consume], c->rx_length);
        if (status == BOOT_PROTOCOL_STATUS_OK) {
            HostStmUpdate_Response(c, &response);
        }
    }
}

void HostStmUpdate_Process(HostStmUpdate_Context *c) {
    if (!c->status.active) {
        return;
    }
    int64_t now = HostStmUpdate_Now(c);
    if (c->status.state == HOST_STM_UPDATE_BOOTING) {
        if (c->ops.runtime_online(c->ops.context) || now >= c->deadline) {
            (void)HostStmUpdate_Enter(c, HOST_STM_UPDATE_VERIFYING);
        }
        return;
    }
    if (c->status.state == HOST_STM_UPDATE_RETURNING) {
        if (c->ops.runtime_online(c->ops.context)) {
            c->status.state = c->outcome;
            c->status.active = false;
        } else if (now >= c->deadline) {
            c->status.state = HOST_STM_UPDATE_FAILED;
            c->status.active = false;
            if (c->status.error == 0) {
                c->status.error = -ETIMEDOUT;
            }
        }
        return;
    }
    if (c->boot_owned) {
        HostStmUpdate_Drain(c);
    }
    if (!c->boot_owned) {
        return;
    }
    if (!c->committed && now - c->activity >= HOST_STM_INACTIVITY_MS) {
        (void)HostStmUpdate_Abort(c);
        return;
    }
    if (!c->outstanding || now < c->deadline) {
        return;
    }
    uint8_t limit = c->pending_command == BOOT_PROTOCOL_COMMAND_QUERY_SLOTS ? 59U : 3U;
    if (c->retries >= limit) {
        HostStmUpdate_Fail(c, -ETIMEDOUT, BOOT_PROTOCOL_STATUS_BUSY);
        return;
    }
    ++c->retries;
    int result = c->ops.send(c->ops.context, c->tx, c->tx_length);
    c->deadline =
        now + (c->pending_command == BOOT_PROTOCOL_COMMAND_QUERY_SLOTS ? HOST_STM_QUERY_MS
                                                                       : HOST_STM_RESPONSE_MS);
    if (result != 0) {
        HostStmUpdate_Fail(c, result, BOOT_PROTOCOL_STATUS_BUSY);
    }
}

bool HostStmUpdate_UsesBootTransport(const HostStmUpdate_Context *c) { return c->boot_owned; }

const char *HostStmUpdate_StateName(HostStmUpdate_State state) {
    static const char *const names[] = {
        "idle",    "querying",  "erasing",   "ready",    "writing",     "finalizing", "pending",
        "booting", "verifying", "returning", "complete", "rolled_back", "failed",     "aborted"};
    return (unsigned)state < sizeof(names) / sizeof(names[0]) ? names[state] : "unknown";
}
