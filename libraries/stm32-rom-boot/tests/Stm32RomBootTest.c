#include "stm32_rom_boot/Stm32RomBoot.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

typedef enum {
    ROM_SYNC,
    ROM_COMMAND,
    ROM_ERASE,
    ROM_WRITE_ADDRESS,
    ROM_WRITE_DATA,
    ROM_READ_ADDRESS,
    ROM_READ_LENGTH
} RomTest_Phase;

typedef struct {
    uint8_t flash[0x40000];
    uint8_t queue[1024];
    size_t queued;
    size_t consumed;
    RomTest_Phase phase;
    uint32_t offset;
    uint16_t id;
    uint8_t id_count;
    uint8_t missing_command;
    uint8_t get_count;
    size_t sends;
    size_t reads;
    size_t fail_send;
    size_t fail_read;
    size_t corrupt_send;
    size_t bad_ack_read;
    uint8_t bad_ack;
    size_t erase_count;
    size_t write_count;
    bool corrupt_readback;
} RomTest_Device;

static RomTest_Device RomTest_DeviceStorage;

static void RomTest_Queue(RomTest_Device *d, uint8_t byte) {
    assert(d->queued < sizeof(d->queue));
    d->queue[d->queued++] = byte;
}
static bool RomTest_Checksum(const uint8_t *data, size_t length) {
    uint8_t checksum = 0U;
    for (size_t i = 0U; i < length; ++i) {
        checksum ^= data[i];
    }
    return checksum == 0U;
}
static void RomTest_Reset(RomTest_Device *d) {
    memset(d, 0, sizeof(*d));
    memset(d->flash, 0xA5, sizeof(d->flash));
    d->id = STM32_ROM_BOOT_DEVICE_ID;
    d->id_count = 1U;
    d->get_count = 5U;
    d->phase = ROM_SYNC;
}
static int RomTest_Send(void *context, const uint8_t *input, size_t length) {
    RomTest_Device *d = context;
    ++d->sends;
    if (d->sends == d->fail_send) {
        return -EIO;
    }
    uint8_t altered[258];
    assert(length <= sizeof(altered));
    memcpy(altered, input, length);
    if (d->sends == d->corrupt_send) {
        altered[length - 1U] ^= 1U;
    }
    const uint8_t *data = altered;
    if (d->phase == ROM_SYNC) {
        assert(length == 1U && data[0] == 0x7FU);
        d->phase = ROM_COMMAND;
        RomTest_Queue(d, 0x79U);
    } else if (d->phase == ROM_COMMAND) {
        assert(length == 2U);
        if ((data[0] ^ data[1]) != 0xFFU) {
            RomTest_Queue(d, 0x1FU);
            return 0;
        }
        RomTest_Queue(d, 0x79U);
        switch (data[0]) {
        case 0x00U: {
            static const uint8_t commands[] = {0x00U, 0x02U, 0x11U, 0x31U, 0x44U};
            RomTest_Queue(d, d->get_count);
            RomTest_Queue(d, 0x31U);
            for (size_t i = 0U; i < sizeof(commands); ++i) {
                RomTest_Queue(d, commands[i] == d->missing_command ? 0x55U : commands[i]);
            }
            RomTest_Queue(d, 0x79U);
            break;
        }
        case 0x02U:
            RomTest_Queue(d, d->id_count);
            RomTest_Queue(d, (uint8_t)(d->id >> 8U));
            RomTest_Queue(d, (uint8_t)d->id);
            RomTest_Queue(d, 0x79U);
            break;
        case 0x44U:
            d->phase = ROM_ERASE;
            break;
        case 0x31U:
            d->phase = ROM_WRITE_ADDRESS;
            break;
        case 0x11U:
            d->phase = ROM_READ_ADDRESS;
            break;
        default:
            assert(false); /* GO, mass erase, option/protection commands forbidden. */
        }
    } else if (d->phase == ROM_ERASE) {
        assert(length == 67U);
        assert(data[0] == 0U && data[1] == 31U);
        assert(data[0] != 0xFFU);
        if (!RomTest_Checksum(data, length)) {
            RomTest_Queue(d, 0x1FU);
        } else {
            for (size_t i = 0U; i < 32U; ++i) {
                assert(data[2U + 2U * i] == 0U);
                assert(data[3U + 2U * i] == i);
            }
            memset(d->flash, 0xFF, STM32_ROM_BOOT_SIZE);
            ++d->erase_count;
            RomTest_Queue(d, 0x79U);
        }
        d->phase = ROM_COMMAND;
    } else if (d->phase == ROM_WRITE_ADDRESS || d->phase == ROM_READ_ADDRESS) {
        assert(length == 5U);
        if (!RomTest_Checksum(data, length)) {
            RomTest_Queue(d, 0x1FU);
            d->phase = ROM_COMMAND;
            return 0;
        }
        uint32_t address = ((uint32_t)data[0] << 24U) | ((uint32_t)data[1] << 16U) |
                           ((uint32_t)data[2] << 8U) | data[3];
        assert(address >= STM32_ROM_BOOT_BASE &&
               address < STM32_ROM_BOOT_BASE + STM32_ROM_BOOT_SIZE);
        d->offset = address - STM32_ROM_BOOT_BASE;
        d->phase = d->phase == ROM_WRITE_ADDRESS ? ROM_WRITE_DATA : ROM_READ_LENGTH;
        RomTest_Queue(d, 0x79U);
    } else if (d->phase == ROM_WRITE_DATA) {
        assert(length >= 10U && length <= 258U);
        size_t count = (size_t)data[0] + 1U;
        assert(count + 2U == length && count % 8U == 0U);
        assert(d->offset % 8U == 0U && count <= STM32_ROM_BOOT_SIZE - d->offset);
        if (!RomTest_Checksum(data, length)) {
            RomTest_Queue(d, 0x1FU);
        } else {
            memcpy(&d->flash[d->offset], &data[1], count);
            ++d->write_count;
            RomTest_Queue(d, 0x79U);
        }
        d->phase = ROM_COMMAND;
    } else if (d->phase == ROM_READ_LENGTH) {
        assert(length == 2U);
        if ((data[0] ^ data[1]) != 0xFFU) {
            RomTest_Queue(d, 0x1FU);
        } else {
            size_t count = (size_t)data[0] + 1U;
            assert(count <= STM32_ROM_BOOT_SIZE - d->offset);
            RomTest_Queue(d, 0x79U);
            for (size_t i = 0U; i < count; ++i) {
                uint8_t value = d->flash[d->offset + i];
                if (d->corrupt_readback && i == count - 1U) {
                    value ^= 1U;
                }
                RomTest_Queue(d, value);
            }
        }
        d->phase = ROM_COMMAND;
    }
    return 0;
}
static int RomTest_Read(void *context, uint8_t *data, size_t length, uint32_t timeout) {
    RomTest_Device *d = context;
    ++d->reads;
    assert(timeout > 0U && timeout <= 10000U);
    if (d->reads == d->fail_read || length > d->queued - d->consumed) {
        return -ETIMEDOUT;
    }
    /* Deliver byte fragments internally; read callback is an exact-length adapter. */
    for (size_t i = 0U; i < length; ++i) {
        data[i] = d->queue[d->consumed++];
    }
    if (d->reads == d->bad_ack_read) {
        assert(length == 1U);
        data[0] = d->bad_ack;
    }
    if (d->consumed == d->queued) {
        d->consumed = 0U;
        d->queued = 0U;
    }
    return 0;
}
static Stm32RomBoot_Context RomTest_Initialize(RomTest_Device *d) {
    RomTest_Reset(d);
    const Stm32RomBoot_Ops ops = {d, RomTest_Send, RomTest_Read};
    Stm32RomBoot_Context context;
    Stm32RomBoot_Initialize(&context, &ops);
    return context;
}
static void RomTest_ProtectedUnchanged(const RomTest_Device *d) {
    for (size_t i = STM32_ROM_BOOT_SIZE; i < sizeof(d->flash); ++i) {
        assert(d->flash[i] == 0xA5U);
    }
}
static void RomTest_Success(void) {
    RomTest_Device *d = &RomTest_DeviceStorage;
    Stm32RomBoot_Context c = RomTest_Initialize(d);
    assert(Stm32RomBoot_Connect(&c) == 0);
    assert(c.connected && !c.erased && c.device_id == 0x489U && c.version == 0x31U);
    uint8_t data[256], actual[256];
    for (size_t i = 0; i < sizeof(data); ++i) {
        data[i] = (uint8_t)i;
    }
    assert(Stm32RomBoot_WriteVerified(&c, 0U, data, sizeof(data)) == -EPERM);
    assert(Stm32RomBoot_EraseBootloader(&c) == 0);
    assert(c.erased && d->erase_count == 1U);
    assert(Stm32RomBoot_WriteVerified(&c, 0U, data, sizeof(data)) == 0);
    assert(Stm32RomBoot_WriteVerified(&c, 256U, data, 13U) == 0);
    assert(Stm32RomBoot_Read(&c, 256U, actual, 16U) == 0);
    assert(memcmp(actual, data, 13U) == 0);
    assert(actual[13] == 0xFFU && actual[14] == 0xFFU && actual[15] == 0xFFU);
    assert(Stm32RomBoot_WriteVerified(&c, STM32_ROM_BOOT_SIZE - 8U, data, 1U) == 0);
    assert(Stm32RomBoot_Read(&c, STM32_ROM_BOOT_SIZE - 1U, actual, 1U) == 0);
    assert(actual[0] == 0xFFU);
    assert(d->write_count == 3U);
    RomTest_ProtectedUnchanged(d);
}
static void RomTest_Preflight(void) {
    RomTest_Device *d = &RomTest_DeviceStorage;
    Stm32RomBoot_Context c = RomTest_Initialize(d);
    uint8_t data[257] = {0};
    assert(Stm32RomBoot_Connect(NULL) == -EINVAL);
    Stm32RomBoot_Context empty;
    Stm32RomBoot_Initialize(&empty, NULL);
    Stm32RomBoot_Initialize(NULL, NULL);
    assert(Stm32RomBoot_Connect(&empty) == -EINVAL);
    assert(Stm32RomBoot_EraseBootloader(NULL) == -EINVAL);
    assert(Stm32RomBoot_EraseBootloader(&c) == -ENOTCONN);
    assert(Stm32RomBoot_Read(&c, 0U, data, 1U) == -ENOTCONN);
    assert(Stm32RomBoot_WriteVerified(&c, 0U, data, 1U) == -ENOTCONN);
    assert(Stm32RomBoot_Connect(&c) == 0);
    size_t sent = d->sends;
    assert(Stm32RomBoot_Read(&c, 0U, NULL, 1U) == -EINVAL);
    assert(Stm32RomBoot_Read(NULL, 0U, data, 1U) == -EINVAL);
    assert(Stm32RomBoot_Read(&c, 0U, data, 0U) == -EINVAL);
    assert(Stm32RomBoot_Read(&c, 0U, data, 257U) == -EINVAL);
    assert(Stm32RomBoot_Read(&c, 65535U, data, 2U) == -ERANGE);
    assert(Stm32RomBoot_Read(&c, UINT32_MAX, data, 1U) == -ERANGE);
    assert(Stm32RomBoot_Read(&c, 65536U, data, 1U) == -ERANGE);
    assert(Stm32RomBoot_WriteVerified(NULL, 0U, data, 1U) == -EINVAL);
    assert(Stm32RomBoot_WriteVerified(&c, 0U, NULL, 1U) == -EINVAL);
    assert(Stm32RomBoot_WriteVerified(&c, 1U, data, 8U) == -EINVAL);
    assert(Stm32RomBoot_WriteVerified(&c, 0U, data, 0U) == -EINVAL);
    assert(Stm32RomBoot_WriteVerified(&c, 0U, data, 257U) == -EINVAL);
    assert(Stm32RomBoot_WriteVerified(&c, 65528U, data, 9U) == -ERANGE);
    assert(Stm32RomBoot_WriteVerified(&c, 65536U, data, 8U) == -ERANGE);
    assert(d->sends == sent && c.connected);
}
static void RomTest_Identification(void) {
    RomTest_Device *d = &RomTest_DeviceStorage;
    Stm32RomBoot_Context c = RomTest_Initialize(d);
    d->id = 0x490U;
    assert(Stm32RomBoot_Connect(&c) == -ENODEV);
    assert(!c.connected && Stm32RomBoot_EraseBootloader(&c) == -ENOTCONN);
    assert(d->erase_count == 0U);
    static const uint8_t mandatory[] = {0x02U, 0x11U, 0x31U, 0x44U};
    for (size_t i = 0; i < sizeof(mandatory); ++i) {
        c = RomTest_Initialize(d);
        d->missing_command = mandatory[i];
        assert(Stm32RomBoot_Connect(&c) == -ENOTSUP);
        assert(!c.connected && d->erase_count == 0U && d->write_count == 0U);
    }
    c = RomTest_Initialize(d);
    d->id_count = 2U;
    assert(Stm32RomBoot_Connect(&c) == -EPROTO);
    c = RomTest_Initialize(d);
    d->get_count = 0xFFU;
    assert(Stm32RomBoot_Connect(&c) == -ETIMEDOUT);
    assert(!c.connected);
    for (size_t read = 1U; read <= 9U; ++read) {
        c = RomTest_Initialize(d);
        d->fail_read = read;
        assert(Stm32RomBoot_Connect(&c) == -ETIMEDOUT);
        assert(!c.connected && d->reads == read && d->erase_count == 0U);
    }
    for (size_t send = 1U; send <= 3U; ++send) {
        c = RomTest_Initialize(d);
        d->fail_send = send;
        assert(Stm32RomBoot_Connect(&c) == -EIO);
        assert(!c.connected && d->sends == send);
    }
    static const uint8_t bad_acks[] = {0x1FU, 0x00U, 0xFFU};
    for (size_t i = 0; i < sizeof(bad_acks); ++i) {
        c = RomTest_Initialize(d);
        d->bad_ack_read = 1U;
        d->bad_ack = bad_acks[i];
        assert(Stm32RomBoot_Connect(&c) == (i == 0U ? -EACCES : -EPROTO));
        assert(!c.connected && d->sends == 1U);
    }
}
static void RomTest_TransactionFaults(void) {
    RomTest_Device *d = &RomTest_DeviceStorage;
    uint8_t data[256];
    memset(data, 0x5A, sizeof(data));
    for (size_t stage = 1U; stage <= 2U; ++stage) {
        Stm32RomBoot_Context c = RomTest_Initialize(d);
        assert(Stm32RomBoot_Connect(&c) == 0);
        d->fail_read = d->reads + stage;
        assert(Stm32RomBoot_EraseBootloader(&c) == -ETIMEDOUT);
        assert(!c.connected && !c.erased);
        assert(d->erase_count == (stage == 2U ? 1U : 0U));
        assert(Stm32RomBoot_EraseBootloader(&c) == -ENOTCONN);
        RomTest_ProtectedUnchanged(d);
    }
    for (size_t stage = 1U; stage <= 2U; ++stage) {
        Stm32RomBoot_Context c = RomTest_Initialize(d);
        assert(Stm32RomBoot_Connect(&c) == 0);
        d->fail_send = d->sends + stage;
        assert(Stm32RomBoot_EraseBootloader(&c) == -EIO);
        assert(!c.connected && !c.erased && d->erase_count == 0U);
        RomTest_ProtectedUnchanged(d);
    }
    for (size_t stage = 1U; stage <= 7U; ++stage) {
        Stm32RomBoot_Context c = RomTest_Initialize(d);
        assert(Stm32RomBoot_Connect(&c) == 0);
        assert(Stm32RomBoot_EraseBootloader(&c) == 0);
        d->fail_read = d->reads + stage;
        assert(Stm32RomBoot_WriteVerified(&c, 0U, data, sizeof(data)) == -ETIMEDOUT);
        assert(!c.connected && !c.erased);
        assert(d->write_count == (stage >= 3U ? 1U : 0U));
        size_t sends = d->sends;
        assert(Stm32RomBoot_WriteVerified(&c, 0U, data, sizeof(data)) == -ENOTCONN);
        assert(d->sends == sends); /* No replay after uncertain write. */
        RomTest_ProtectedUnchanged(d);
    }
    for (size_t stage = 1U; stage <= 6U; ++stage) {
        Stm32RomBoot_Context c = RomTest_Initialize(d);
        assert(Stm32RomBoot_Connect(&c) == 0);
        assert(Stm32RomBoot_EraseBootloader(&c) == 0);
        d->corrupt_send = d->sends + stage;
        assert(Stm32RomBoot_WriteVerified(&c, 0U, data, sizeof(data)) == -EACCES);
        assert(!c.connected && !c.erased);
        RomTest_ProtectedUnchanged(d);
    }
    for (size_t stage = 1U; stage <= 6U; ++stage) {
        Stm32RomBoot_Context c = RomTest_Initialize(d);
        assert(Stm32RomBoot_Connect(&c) == 0);
        assert(Stm32RomBoot_EraseBootloader(&c) == 0);
        d->fail_send = d->sends + stage;
        assert(Stm32RomBoot_WriteVerified(&c, 0U, data, sizeof(data)) == -EIO);
        assert(!c.connected && !c.erased);
        assert(d->write_count == (stage > 3U ? 1U : 0U));
        RomTest_ProtectedUnchanged(d);
    }
    Stm32RomBoot_Context c = RomTest_Initialize(d);
    assert(Stm32RomBoot_Connect(&c) == 0);
    d->corrupt_send = d->sends + 2U;
    assert(Stm32RomBoot_EraseBootloader(&c) == -EACCES);
    assert(d->erase_count == 0U);
    c = RomTest_Initialize(d);
    assert(Stm32RomBoot_Connect(&c) == 0);
    assert(Stm32RomBoot_EraseBootloader(&c) == 0);
    d->corrupt_readback = true;
    assert(Stm32RomBoot_WriteVerified(&c, 0U, data, 13U) == -EBADMSG);
    assert(!c.connected && !c.erased && d->write_count == 1U);
    RomTest_ProtectedUnchanged(d);
}

static void RomTest_FullBoundedRepair(void) {
    RomTest_Device *d = &RomTest_DeviceStorage;
    Stm32RomBoot_Context c = RomTest_Initialize(d);
    assert(Stm32RomBoot_Connect(&c) == 0);
    assert(Stm32RomBoot_EraseBootloader(&c) == 0);
    uint8_t data[256];
    for (uint32_t offset = 0U; offset < STM32_ROM_BOOT_SIZE; offset += sizeof(data)) {
        for (size_t i = 0U; i < sizeof(data); ++i) {
            data[i] = (uint8_t)(offset / 256U + i);
        }
        assert(Stm32RomBoot_WriteVerified(&c, offset, data, sizeof(data)) == 0);
        assert(memcmp(&d->flash[offset], data, sizeof(data)) == 0);
    }
    assert(d->erase_count == 1U && d->write_count == 256U && c.connected);
    RomTest_ProtectedUnchanged(d);
}
int main(void) {
    RomTest_Success();
    RomTest_Preflight();
    RomTest_Identification();
    RomTest_TransactionFaults();
    RomTest_FullBoundedRepair();
    puts("STM32 ROM transaction emulator tests passed");
    return 0;
}
