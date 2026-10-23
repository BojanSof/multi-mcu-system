#include "stm32_rom_boot/Stm32RomBoot.h"

#include <errno.h>
#include <string.h>

#define STM32_ROM_BOOT_ACK 0x79U
#define STM32_ROM_BOOT_NACK 0x1FU
#define STM32_ROM_BOOT_TIMEOUT_MS 1000U
#define STM32_ROM_BOOT_ERASE_TIMEOUT_MS 10000U

static int Stm32RomBoot_Fail(Stm32RomBoot_Context *c, int result) {
    c->connected = false;
    c->erased = false;
    return result < 0 ? result : -EIO;
}

static int Stm32RomBoot_Send(Stm32RomBoot_Context *c, const uint8_t *data, size_t length) {
    const int result = c->ops.send(c->ops.context, data, length);
    return result == 0 ? 0 : Stm32RomBoot_Fail(c, result);
}

static int Stm32RomBoot_Receive(Stm32RomBoot_Context *c, uint8_t *data, size_t length,
                                uint32_t timeout) {
    const int result = c->ops.read(c->ops.context, data, length, timeout);
    return result == 0 ? 0 : Stm32RomBoot_Fail(c, result);
}

static int Stm32RomBoot_Acknowledge(Stm32RomBoot_Context *c, uint32_t timeout) {
    uint8_t value;
    int result = Stm32RomBoot_Receive(c, &value, 1U, timeout);
    if (result != 0) {
        return result;
    }
    if (value != STM32_ROM_BOOT_ACK) {
        return Stm32RomBoot_Fail(c, value == STM32_ROM_BOOT_NACK ? -EACCES : -EPROTO);
    }
    return 0;
}

static int Stm32RomBoot_Command(Stm32RomBoot_Context *c, uint8_t command) {
    const uint8_t frame[2] = {command, (uint8_t)(command ^ 0xFFU)};
    int result = Stm32RomBoot_Send(c, frame, sizeof(frame));
    return result == 0 ? Stm32RomBoot_Acknowledge(c, STM32_ROM_BOOT_TIMEOUT_MS) : result;
}

static int Stm32RomBoot_Address(Stm32RomBoot_Context *c, uint32_t offset) {
    const uint32_t address = STM32_ROM_BOOT_BASE + offset;
    uint8_t frame[5];
    frame[0] = (uint8_t)(address >> 24U);
    frame[1] = (uint8_t)(address >> 16U);
    frame[2] = (uint8_t)(address >> 8U);
    frame[3] = (uint8_t)address;
    frame[4] = frame[0] ^ frame[1] ^ frame[2] ^ frame[3];
    int result = Stm32RomBoot_Send(c, frame, sizeof(frame));
    return result == 0 ? Stm32RomBoot_Acknowledge(c, STM32_ROM_BOOT_TIMEOUT_MS) : result;
}

static int Stm32RomBoot_CheckRange(uint32_t offset, size_t length) {
    if (length == 0U || length > STM32_ROM_BOOT_CHUNK_SIZE) {
        return -EINVAL;
    }
    if (offset >= STM32_ROM_BOOT_SIZE || length > STM32_ROM_BOOT_SIZE - offset) {
        return -ERANGE;
    }
    return 0;
}

void Stm32RomBoot_Initialize(Stm32RomBoot_Context *c, const Stm32RomBoot_Ops *ops) {
    if (c == NULL) {
        return;
    }
    memset(c, 0, sizeof(*c));
    if (ops != NULL) {
        c->ops = *ops;
    }
}

int Stm32RomBoot_Connect(Stm32RomBoot_Context *c) {
    if (c == NULL || c->ops.send == NULL || c->ops.read == NULL) {
        return -EINVAL;
    }
    /* Call only after a fresh external reset into ROM. A lost ACK is ambiguous:
     * repeated sync could become a command byte, so there is no blind replay. */
    c->connected = false;
    c->erased = false;
    c->device_id = 0U;
    c->version = 0U;
    const uint8_t sync = 0x7FU;
    int result = Stm32RomBoot_Send(c, &sync, 1U);
    if (result == 0) {
        result = Stm32RomBoot_Acknowledge(c, STM32_ROM_BOOT_TIMEOUT_MS);
    }
    if (result == 0) {
        result = Stm32RomBoot_Command(c, 0x00U);
    }
    uint8_t count = 0U;
    uint8_t response[256];
    if (result == 0) {
        result = Stm32RomBoot_Receive(c, &count, 1U, STM32_ROM_BOOT_TIMEOUT_MS);
    }
    if (result == 0) {
        result = Stm32RomBoot_Receive(c, response, (size_t)count + 1U, STM32_ROM_BOOT_TIMEOUT_MS);
    }
    if (result == 0) {
        result = Stm32RomBoot_Acknowledge(c, STM32_ROM_BOOT_TIMEOUT_MS);
    }
    if (result != 0) {
        return result;
    }
    c->version = response[0];
    bool has_id = false, has_read = false, has_write = false, has_erase = false;
    for (size_t i = 1U; i <= count; ++i) {
        has_id |= response[i] == 0x02U;
        has_read |= response[i] == 0x11U;
        has_write |= response[i] == 0x31U;
        has_erase |= response[i] == 0x44U;
    }
    if (!has_id || !has_read || !has_write || !has_erase || c->version == 0U ||
        c->version == 0xFFU) {
        return Stm32RomBoot_Fail(c, -ENOTSUP);
    }
    result = Stm32RomBoot_Command(c, 0x02U);
    if (result == 0) {
        result = Stm32RomBoot_Receive(c, &count, 1U, STM32_ROM_BOOT_TIMEOUT_MS);
    }
    if (result != 0) {
        return result;
    }
    if (count != 1U) {
        return Stm32RomBoot_Fail(c, -EPROTO);
    }
    result = Stm32RomBoot_Receive(c, response, 2U, STM32_ROM_BOOT_TIMEOUT_MS);
    if (result == 0) {
        result = Stm32RomBoot_Acknowledge(c, STM32_ROM_BOOT_TIMEOUT_MS);
    }
    if (result != 0) {
        return result;
    }
    c->device_id = (uint16_t)(((uint16_t)response[0] << 8U) | response[1]);
    if (c->device_id != STM32_ROM_BOOT_DEVICE_ID) {
        return Stm32RomBoot_Fail(c, -ENODEV);
    }
    c->connected = true;
    return 0;
}

int Stm32RomBoot_EraseBootloader(Stm32RomBoot_Context *c) {
    if (c == NULL) {
        return -EINVAL;
    }
    if (!c->connected) {
        return -ENOTCONN;
    }
    /* Deliberately fixed page list; special/mass erase encodings cannot occur. */
    enum { page_count = STM32_ROM_BOOT_SIZE / STM32_ROM_BOOT_PAGE_SIZE };
    uint8_t frame[2U + page_count * 2U + 1U];
    frame[0] = 0U;
    frame[1] = (uint8_t)(page_count - 1U);
    uint8_t checksum = frame[1];
    for (size_t page = 0U; page < page_count; ++page) {
        frame[2U + page * 2U] = 0U;
        frame[3U + page * 2U] = (uint8_t)page;
        checksum ^= (uint8_t)page;
    }
    frame[sizeof(frame) - 1U] = checksum;
    c->erased = false;
    int result = Stm32RomBoot_Command(c, 0x44U);
    if (result == 0) {
        result = Stm32RomBoot_Send(c, frame, sizeof(frame));
    }
    if (result == 0) {
        result = Stm32RomBoot_Acknowledge(c, STM32_ROM_BOOT_ERASE_TIMEOUT_MS);
    }
    c->erased = result == 0;
    return result;
}

int Stm32RomBoot_Read(Stm32RomBoot_Context *c, uint32_t offset, uint8_t *data, size_t length) {
    if (c == NULL || data == NULL) {
        return -EINVAL;
    }
    int result = Stm32RomBoot_CheckRange(offset, length);
    if (result != 0) {
        return result;
    }
    if (!c->connected) {
        return -ENOTCONN;
    }
    result = Stm32RomBoot_Command(c, 0x11U);
    if (result == 0) {
        result = Stm32RomBoot_Address(c, offset);
    }
    const uint8_t count[2] = {(uint8_t)(length - 1U), (uint8_t)((length - 1U) ^ 0xFFU)};
    if (result == 0) {
        result = Stm32RomBoot_Send(c, count, sizeof(count));
    }
    if (result == 0) {
        result = Stm32RomBoot_Acknowledge(c, STM32_ROM_BOOT_TIMEOUT_MS);
    }
    if (result == 0) {
        result = Stm32RomBoot_Receive(c, data, length, STM32_ROM_BOOT_TIMEOUT_MS);
    }
    return result;
}

int Stm32RomBoot_WriteVerified(Stm32RomBoot_Context *c, uint32_t offset, const uint8_t *data,
                               size_t length) {
    if (c == NULL || data == NULL || (offset & 7U) != 0U) {
        return -EINVAL;
    }
    int result = Stm32RomBoot_CheckRange(offset, length);
    if (result != 0) {
        return result;
    }
    const size_t padded = (length + 7U) & ~(size_t)7U;
    result = Stm32RomBoot_CheckRange(offset, padded);
    if (result != 0) {
        return result;
    }
    if (!c->connected) {
        return -ENOTCONN;
    }
    if (!c->erased) {
        return -EPERM;
    }
    uint8_t frame[STM32_ROM_BOOT_CHUNK_SIZE + 2U];
    frame[0] = (uint8_t)(padded - 1U);
    memcpy(&frame[1], data, length);
    memset(&frame[1U + length], 0xFF, padded - length);
    uint8_t checksum = frame[0];
    for (size_t i = 1U; i <= padded; ++i) {
        checksum ^= frame[i];
    }
    frame[padded + 1U] = checksum;
    result = Stm32RomBoot_Command(c, 0x31U);
    if (result == 0) {
        result = Stm32RomBoot_Address(c, offset);
    }
    if (result == 0) {
        result = Stm32RomBoot_Send(c, frame, padded + 2U);
    }
    if (result == 0) {
        result = Stm32RomBoot_Acknowledge(c, STM32_ROM_BOOT_TIMEOUT_MS);
    }
    uint8_t actual[STM32_ROM_BOOT_CHUNK_SIZE];
    if (result == 0) {
        result = Stm32RomBoot_Read(c, offset, actual, padded);
    }
    if (result == 0 && memcmp(actual, &frame[1], padded) != 0) {
        result = Stm32RomBoot_Fail(c, -EBADMSG);
    }
    return result;
}
