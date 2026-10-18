#include "boot_protocol/BootProtocol.h"
#include "firmware_image/FirmwareImage.h"
#include "flash_layout.h"
#include "mbedtls/sha256.h"
#include "stm32u083xx.h"
#include "uECC.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#ifndef BOOTLOADER_PUBLIC_KEY_HEADER
#error                                                                                             \
    "BOOTLOADER_PUBLIC_KEY_HEADER must name an external header defining BOOTLOADER_PUBLIC_KEY_BYTES and BOOTLOADER_PUBLIC_KEY_ID"
#endif
#include BOOTLOADER_PUBLIC_KEY_HEADER
static const uint8_t BootVerify_PublicKey[64] = BOOTLOADER_PUBLIC_KEY_BYTES;
bool BootVerify_Slot(uint8_t slot, uint32_t expected_size) {
    if (slot >= 2U)
        return false;
    const uint32_t base = slot == 0U ? MULTI_MCU_SLOT0_ORIGIN : MULTI_MCU_SLOT1_ORIGIN;
    const uint32_t vector =
        slot == 0U ? MULTI_MCU_SLOT0_VECTOR_ORIGIN : MULTI_MCU_SLOT1_VECTOR_ORIGIN;
    const uint8_t *image = (const uint8_t *)(uintptr_t)base;
    FirmwareImage_SignedManifest manifest;
    const uint8_t *payload;
    uint32_t image_size = FIRMWARE_IMAGE_SIGNED_HEADER_SIZE;
    if (FirmwareImage_ParseSignedHeader(image, FIRMWARE_IMAGE_SIGNED_HEADER_SIZE, &manifest,
                                        &payload) == FIRMWARE_IMAGE_STATUS_SIZE_MISMATCH) {
        const uint32_t payload_size = (uint32_t)image[32] | ((uint32_t)image[33] << 8U) |
                                      ((uint32_t)image[34] << 16U) | ((uint32_t)image[35] << 24U);
        if (payload_size > FIRMWARE_IMAGE_STM32_MAX_PAYLOAD_SIZE)
            return false;
        image_size += payload_size;
    }
    if (image_size > MULTI_MCU_SLOT_SIZE || (expected_size != 0U && image_size != expected_size) ||
        FirmwareImage_ParseSignedHeader(image, image_size, &manifest, &payload) !=
            FIRMWARE_IMAGE_STATUS_OK)
        return false;
    const uint32_t initial_stack = *(const uint32_t *)(uintptr_t)vector;
    const uint32_t reset_vector = *(const uint32_t *)(uintptr_t)(vector + 4U);
    const uint32_t reset_address = reset_vector & ~1UL;
    if (initial_stack < SRAM1_BASE || initial_stack > SRAM1_BASE + 0xA000UL ||
        (initial_stack & 7U) != 0U || (reset_vector & 1U) == 0U || reset_address < vector ||
        reset_address >= base + MULTI_MCU_SLOT_SIZE)
        return false;
    FirmwareImage_SignedCompatibility compatibility = {FIRMWARE_IMAGE_TARGET_STM32U083_APPLICATION,
                                                       FIRMWARE_IMAGE_HARDWARE_NUCLEO_U083RC,
                                                       slot,
                                                       vector,
                                                       FIRMWARE_IMAGE_STM32_MAX_PAYLOAD_SIZE,
                                                       FIRMWARE_IMAGE_SIGNATURE_ECDSA_P256_SHA256,
                                                       BOOTLOADER_PUBLIC_KEY_ID,
                                                       BOOT_PROTOCOL_VERSION_MAJOR,
                                                       BOOT_PROTOCOL_VERSION_MINOR,
                                                       BOOT_PROTOCOL_VERSION_MAJOR,
                                                       BOOT_PROTOCOL_VERSION_MINOR};
    if (FirmwareImage_ValidateSignedCompatibility(&manifest, &compatibility) !=
        FIRMWARE_IMAGE_STATUS_OK)
        return false;
    uint8_t payload_hash[32], signed_hash[32];
    if (mbedtls_sha256(payload, manifest.manifest.payload_size, payload_hash, 0) != 0 ||
        FirmwareImage_ValidateHash(&manifest.manifest, payload_hash) != FIRMWARE_IMAGE_STATUS_OK ||
        mbedtls_sha256(image, FIRMWARE_IMAGE_SIGNED_PREFIX_SIZE, signed_hash, 0) != 0)
        return false;
    return uECC_verify(BootVerify_PublicKey, signed_hash, sizeof(signed_hash), manifest.signature,
                       uECC_secp256r1()) == 1;
}
