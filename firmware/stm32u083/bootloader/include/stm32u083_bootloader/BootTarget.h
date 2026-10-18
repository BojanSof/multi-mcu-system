#ifndef STM32U083_BOOTLOADER_BOOT_TARGET_H
#define STM32U083_BOOTLOADER_BOOT_TARGET_H
#include "stm32u083_bootloader/BootConfirm.h"
#include "stm32u083_bootloader/BootEngine.h"
#include <stdbool.h>
#include <stdint.h>
void BootTarget_Initialize(void);
BootJournal_Flash BootTarget_Journal(void);
BootEngine_Storage BootTarget_Storage(void);
uint8_t BootTarget_ValidSlotMask(void);
void BootTarget_ConsumeConfirmation(void);
void BootTarget_SetTrialMarker(bool trial, uint8_t slot);
bool BootTarget_Jump(uint8_t slot);
bool BootTarget_RecoveryRequested(uint32_t timeout_ms);
void BootTarget_RunRecovery(uint8_t active_slot);
#endif
