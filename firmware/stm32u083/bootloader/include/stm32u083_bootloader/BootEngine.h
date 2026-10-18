#ifndef STM32U083_BOOTLOADER_BOOT_ENGINE_H
#define STM32U083_BOOTLOADER_BOOT_ENGINE_H
#include "boot_protocol/BootProtocol.h"
#include "stm32u083_bootloader/BootJournal.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
    void *context;
    BootProtocol_Status (*erase_slot)(void *context, uint8_t slot);
    BootProtocol_Status (*write)(void *context, uint8_t slot, uint32_t offset, const uint8_t *data,
                                 size_t size);
    bool (*validate_slot)(void *context, uint8_t slot, uint32_t image_size);
    void (*reboot)(void *context);
} BootEngine_Storage;
typedef struct {
    BootJournal_Flash journal;
    BootEngine_Storage storage;
    uint32_t next_offset;
    uint32_t finalized_size;
    uint8_t update_slot;
    uint8_t active_slot;
    bool erased;
    bool finalized;
    bool cached;
    bool sequence_initialized;
    uint16_t expected_sequence;
    BootProtocol_Message cached_request;
    BootProtocol_Message cached_response;
} BootEngine_Context;
void BootEngine_Init(BootEngine_Context *context, const BootJournal_Flash *journal,
                     const BootEngine_Storage *storage);
void BootEngine_SetActiveSlot(BootEngine_Context *context, uint8_t active_slot);
BootProtocol_Status BootEngine_Process(BootEngine_Context *context,
                                       const BootProtocol_Message *request,
                                       BootProtocol_Message *response);
#endif
