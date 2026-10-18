#ifndef STM32U083_BOOTLOADER_BOOT_CONFIRM_H
#define STM32U083_BOOTLOADER_BOOT_CONFIRM_H
#include <stdint.h>
/* BKP0: written by a trial application before reset, consumed and cleared by bootloader. */
#define BOOT_CONFIRM_APPLICATION_BASE UINT32_C(0x004F4E46)
/* BKP1: written by bootloader before a trial jump, cleared before confirmed/recovery boot. */
#define BOOT_CONFIRM_TRIAL_BASE UINT32_C(0x00524954)
#define BOOT_CONFIRM_SLOT_SHIFT 24U
#define BOOT_CONFIRM_MARKER(base, slot)                                                            \
    ((uint32_t)(base) | ((uint32_t)(slot) << BOOT_CONFIRM_SLOT_SHIFT))
#endif
