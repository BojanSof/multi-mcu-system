#include "HostBootloaderClient.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>

#include "HostTransport.h"
#include "boot_protocol/BootProtocol.h"
#include "firmware_image/FirmwareImage.h"

#define HOST_BOOTLOADER_QUERY_RETRY_MS 250
#define HOST_BOOTLOADER_QUERY_MAX_RETRIES 60U
#define HOST_BOOTLOADER_RESPONSE_TIMEOUT_MS 5000
#define HOST_BOOTLOADER_MAX_RETRIES 3U
#define HOST_BOOTLOADER_RX_CAPACITY (BOOT_PROTOCOL_MAX_FRAME_SIZE * 2U)

typedef struct {
    HostBootloaderClient_Status status;
    const uint8_t *image;
    size_t image_size;
    uint8_t rx[HOST_BOOTLOADER_RX_CAPACITY];
    size_t rx_size;
    uint8_t tx[BOOT_PROTOCOL_MAX_FRAME_SIZE];
    size_t tx_size;
    uint8_t outstanding_command;
    uint16_t outstanding_sequence;
    uint16_t outstanding_payload_length;
    uint8_t retries;
    int64_t deadline;
    bool request_outstanding;
} HostBootloaderClient_Context;

static HostBootloaderClient_Context HostBootloaderClient;

static int64_t HostBootloaderClient_RetryInterval(uint8_t command) {
    return command == BOOT_PROTOCOL_COMMAND_QUERY_SLOTS ? HOST_BOOTLOADER_QUERY_RETRY_MS
                                                        : HOST_BOOTLOADER_RESPONSE_TIMEOUT_MS;
}

static uint8_t HostBootloaderClient_RetryLimit(uint8_t command) {
    return command == BOOT_PROTOCOL_COMMAND_QUERY_SLOTS ? HOST_BOOTLOADER_QUERY_MAX_RETRIES
                                                        : HOST_BOOTLOADER_MAX_RETRIES;
}

static uint16_t HostBootloaderClient_ReadU16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8U));
}

static void HostBootloaderClient_Fail(int error, uint8_t status) {
    HostBootloaderClient.status.state = HOST_BOOTLOADER_CLIENT_FAILED;
    HostBootloaderClient.status.last_error = error;
    HostBootloaderClient.status.last_bootloader_status = status;
    HostBootloaderClient.request_outstanding = false;
}

static int HostBootloaderClient_Send(uint8_t command, uint32_t offset, const uint8_t *payload,
                                     uint16_t payload_length,
                                     HostBootloaderClient_State waiting_state) {
    BootProtocol_Message message = {0};
    BootProtocol_Status protocol_status;
    int result;

    if (HostBootloaderClient.request_outstanding) {
        return -EBUSY;
    }
    message.command = command;
    message.status = BOOT_PROTOCOL_STATUS_OK;
    message.sequence = HostBootloaderClient.status.next_sequence++;
    if (command != BOOT_PROTOCOL_COMMAND_QUERY_SLOTS &&
        command != BOOT_PROTOCOL_COMMAND_GET_BOOT_STATE &&
        command != BOOT_PROTOCOL_COMMAND_REBOOT) {
        message.flags = HostBootloaderClient.status.target_slot & BOOT_PROTOCOL_FLAG_SLOT_MASK;
    }
    message.operation_offset = offset;
    message.payload_length = payload_length;
    if (payload_length != 0U) {
        if (payload == NULL) {
            return -EINVAL;
        }
        (void)memcpy(message.payload, payload, payload_length);
    }
    protocol_status =
        BootProtocol_Encode(&message, HostBootloaderClient.tx, sizeof(HostBootloaderClient.tx),
                            &HostBootloaderClient.tx_size);
    if (protocol_status != BOOT_PROTOCOL_STATUS_OK) {
        return -EINVAL;
    }
    result = HostTransport_Write(HostBootloaderClient.tx, HostBootloaderClient.tx_size);
    if (result != 0) {
        return result;
    }
    HostBootloaderClient.outstanding_command = command;
    HostBootloaderClient.outstanding_sequence = message.sequence;
    HostBootloaderClient.outstanding_payload_length = payload_length;
    HostBootloaderClient.retries = 0U;
    HostBootloaderClient.deadline = k_uptime_get() + HostBootloaderClient_RetryInterval(command);
    HostBootloaderClient.request_outstanding = true;
    HostBootloaderClient.status.state = waiting_state;
    return 0;
}

static int HostBootloaderClient_SendNextChunk(void) {
    const size_t remaining =
        HostBootloaderClient.image_size - (size_t)HostBootloaderClient.status.bytes_written;
    const uint16_t chunk_size = (uint16_t)MIN(remaining, (size_t)BOOT_PROTOCOL_MAX_PAYLOAD_SIZE);

    if (chunk_size == 0U) {
        return HostBootloaderClient_Send(BOOT_PROTOCOL_COMMAND_FINALIZE,
                                         (uint32_t)HostBootloaderClient.image_size, NULL, 0U,
                                         HOST_BOOTLOADER_CLIENT_FINALIZING);
    }
    return HostBootloaderClient_Send(
        BOOT_PROTOCOL_COMMAND_WRITE_CHUNK, HostBootloaderClient.status.bytes_written,
        &HostBootloaderClient.image[HostBootloaderClient.status.bytes_written], chunk_size,
        HOST_BOOTLOADER_CLIENT_WRITING);
}

static void HostBootloaderClient_Advance(const BootProtocol_Message *response) {
    int result = 0;

    HostBootloaderClient.status.last_bootloader_status = response->status;
    if (response->status != BOOT_PROTOCOL_STATUS_OK) {
        HostBootloaderClient_Fail(-EIO, response->status);
        return;
    }
    switch (HostBootloaderClient.status.state) {
    case HOST_BOOTLOADER_CLIENT_QUERYING:
        if (response->payload_length != BOOT_PROTOCOL_QUERY_RESPONSE_SIZE ||
            (response->payload[4] > 1U && response->payload[4] != BOOT_PROTOCOL_NO_SLOT)) {
            HostBootloaderClient_Fail(-EPROTO, BOOT_PROTOCOL_STATUS_BAD_FRAME);
            return;
        }
        HostBootloaderClient.status.target_slot = response->payload[4] == BOOT_PROTOCOL_NO_SLOT
                                                      ? BOOT_PROTOCOL_NO_SLOT
                                                      : response->payload[4] ^ 1U;
        HostBootloaderClient.status.state = HOST_BOOTLOADER_CLIENT_READY;
        break;
    case HOST_BOOTLOADER_CLIENT_ERASING:
        result = HostBootloaderClient_SendNextChunk();
        break;
    case HOST_BOOTLOADER_CLIENT_WRITING:
        HostBootloaderClient.status.bytes_written +=
            HostBootloaderClient.outstanding_payload_length;
        result = HostBootloaderClient_SendNextChunk();
        break;
    case HOST_BOOTLOADER_CLIENT_FINALIZING:
        result = HostBootloaderClient_Send(BOOT_PROTOCOL_COMMAND_SET_PENDING, 0U, NULL, 0U,
                                           HOST_BOOTLOADER_CLIENT_SETTING_PENDING);
        break;
    case HOST_BOOTLOADER_CLIENT_SETTING_PENDING:
        result = HostBootloaderClient_Send(BOOT_PROTOCOL_COMMAND_GET_BOOT_STATE, 0U, NULL, 0U,
                                           HOST_BOOTLOADER_CLIENT_VERIFYING_STATE);
        break;
    case HOST_BOOTLOADER_CLIENT_VERIFYING_STATE:
        if (response->payload_length != BOOT_PROTOCOL_BOOT_STATE_RESPONSE_SIZE ||
            response->payload[2] != HostBootloaderClient.status.target_slot) {
            HostBootloaderClient_Fail(-EPROTO, BOOT_PROTOCOL_STATUS_STATE);
            return;
        }
        HostBootloaderClient.status.state = HOST_BOOTLOADER_CLIENT_COMPLETE;
        break;
    case HOST_BOOTLOADER_CLIENT_REBOOTING:
        HostBootloaderClient.status.state = HOST_BOOTLOADER_CLIENT_IDLE;
        break;
    default:
        HostBootloaderClient_Fail(-EPROTO, BOOT_PROTOCOL_STATUS_STATE);
        return;
    }
    if (result != 0) {
        HostBootloaderClient_Fail(result, BOOT_PROTOCOL_STATUS_BUSY);
    }
}

static void HostBootloaderClient_HandleFrame(const uint8_t *frame, size_t frame_size) {
    BootProtocol_Message response;
    BootProtocol_Status status = BootProtocol_Decode(frame, frame_size, &response);

    if (status != BOOT_PROTOCOL_STATUS_OK || !HostBootloaderClient.request_outstanding ||
        (response.flags & BOOT_PROTOCOL_FLAG_RESPONSE) == 0U ||
        response.command != HostBootloaderClient.outstanding_command ||
        response.sequence != HostBootloaderClient.outstanding_sequence) {
        return;
    }
    HostBootloaderClient.request_outstanding = false;
    HostBootloaderClient_Advance(&response);
}

static void HostBootloaderClient_DrainRx(void) {
    size_t available = sizeof(HostBootloaderClient.rx) - HostBootloaderClient.rx_size;
    size_t received =
        HostTransport_Read(&HostBootloaderClient.rx[HostBootloaderClient.rx_size], available);
    HostBootloaderClient.rx_size += received;

    while (HostBootloaderClient.rx_size >= BOOT_PROTOCOL_HEADER_SIZE) {
        size_t frame_size;
        uint16_t payload_length;

        if (HostBootloaderClient.rx[0] != (uint8_t)BOOT_PROTOCOL_MAGIC_0 ||
            HostBootloaderClient.rx[1] != (uint8_t)BOOT_PROTOCOL_MAGIC_1) {
            (void)memmove(HostBootloaderClient.rx, &HostBootloaderClient.rx[1],
                          --HostBootloaderClient.rx_size);
            continue;
        }
        payload_length = HostBootloaderClient_ReadU16(&HostBootloaderClient.rx[8]);
        if (payload_length > BOOT_PROTOCOL_MAX_PAYLOAD_SIZE) {
            (void)memmove(HostBootloaderClient.rx, &HostBootloaderClient.rx[1],
                          --HostBootloaderClient.rx_size);
            continue;
        }
        frame_size = BOOT_PROTOCOL_HEADER_SIZE + (size_t)payload_length;
        if (HostBootloaderClient.rx_size < frame_size) {
            break;
        }
        HostBootloaderClient_HandleFrame(HostBootloaderClient.rx, frame_size);
        HostBootloaderClient.rx_size -= frame_size;
        (void)memmove(HostBootloaderClient.rx, &HostBootloaderClient.rx[frame_size],
                      HostBootloaderClient.rx_size);
    }
    if (HostBootloaderClient.rx_size == sizeof(HostBootloaderClient.rx)) {
        HostBootloaderClient.rx_size = 0U;
    }
}

void HostBootloaderClient_Initialize(void) {
    (void)memset(&HostBootloaderClient, 0, sizeof(HostBootloaderClient));
    HostBootloaderClient.status.next_sequence = 1U;
    HostBootloaderClient.status.target_slot = BOOT_PROTOCOL_NO_SLOT;
}

int HostBootloaderClient_Query(void) {
    return HostBootloaderClient_Send(BOOT_PROTOCOL_COMMAND_QUERY_SLOTS, 0U, NULL, 0U,
                                     HOST_BOOTLOADER_CLIENT_QUERYING);
}

int HostBootloaderClient_StartUpdate(const uint8_t *image, size_t image_size) {
    FirmwareImage_SignedManifest manifest;
    const uint8_t *payload;

    if (image == NULL || image_size == 0U || image_size > UINT32_MAX ||
        HostBootloaderClient.request_outstanding ||
        HostBootloaderClient.status.state != HOST_BOOTLOADER_CLIENT_READY) {
        return -EINVAL;
    }
    if (FirmwareImage_ParseSignedHeader(image, image_size, &manifest, &payload) !=
        FIRMWARE_IMAGE_STATUS_OK) {
        return -EBADMSG;
    }
    if (HostBootloaderClient.status.target_slot == BOOT_PROTOCOL_NO_SLOT) {
        HostBootloaderClient.status.target_slot = manifest.slot_id;
    } else if (HostBootloaderClient.status.target_slot != manifest.slot_id) {
        return -EXDEV;
    }
    HostBootloaderClient.image = image;
    HostBootloaderClient.image_size = image_size;
    HostBootloaderClient.status.image_size = (uint32_t)image_size;
    HostBootloaderClient.status.bytes_written = 0U;
    return HostBootloaderClient_Send(BOOT_PROTOCOL_COMMAND_ERASE_INACTIVE, (uint32_t)image_size,
                                     NULL, 0U, HOST_BOOTLOADER_CLIENT_ERASING);
}

int HostBootloaderClient_Reboot(void) {
    return HostBootloaderClient_Send(BOOT_PROTOCOL_COMMAND_REBOOT, 0U, NULL, 0U,
                                     HOST_BOOTLOADER_CLIENT_REBOOTING);
}

void HostBootloaderClient_Process(void) {
    HostBootloaderClient_DrainRx();
    if (!HostBootloaderClient.request_outstanding ||
        k_uptime_get() < HostBootloaderClient.deadline) {
        return;
    }
    const uint8_t retry_limit =
        HostBootloaderClient_RetryLimit(HostBootloaderClient.outstanding_command);
    if (HostBootloaderClient.retries >= retry_limit) {
        HostBootloaderClient_Fail(-ETIMEDOUT, BOOT_PROTOCOL_STATUS_BUSY);
        return;
    }
    const int result = HostTransport_Write(HostBootloaderClient.tx, HostBootloaderClient.tx_size);
    HostBootloaderClient.retries++;
    HostBootloaderClient.deadline = k_uptime_get() + HostBootloaderClient_RetryInterval(
                                                         HostBootloaderClient.outstanding_command);
    if (result != 0 && HostBootloaderClient.retries >= retry_limit) {
        HostBootloaderClient_Fail(result, BOOT_PROTOCOL_STATUS_BUSY);
    }
}

HostBootloaderClient_Status HostBootloaderClient_GetStatus(void) {
    return HostBootloaderClient.status;
}

const char *HostBootloaderClient_StateName(HostBootloaderClient_State state) {
    static const char *const names[] = {"idle",     "querying",   "ready",   "erasing",
                                        "writing",  "finalizing", "pending", "verifying",
                                        "complete", "rebooting",  "failed"};
    return state <= HOST_BOOTLOADER_CLIENT_FAILED ? names[state] : "unknown";
}
