#ifndef FIRMWARE_IMAGE_FIRMWARE_IMAGE_H
#define FIRMWARE_IMAGE_FIRMWARE_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FIRMWARE_IMAGE_MAGIC_0 'M'
#define FIRMWARE_IMAGE_MAGIC_1 'M'
#define FIRMWARE_IMAGE_MAGIC_2 'F'
#define FIRMWARE_IMAGE_MAGIC_3 'W'
#define FIRMWARE_IMAGE_FORMAT_MAJOR 1U
#define FIRMWARE_IMAGE_FORMAT_MINOR 0U
#define FIRMWARE_IMAGE_MANIFEST_SIZE 80U
#define FIRMWARE_IMAGE_SHA256_SIZE 32U
#define FIRMWARE_IMAGE_TARGET_NRF54L15_HOST 1U
#define FIRMWARE_IMAGE_TARGET_STM32U083_APPLICATION 2U
#define FIRMWARE_IMAGE_HARDWARE_NRF54L15_DK 0x54150001UL
#define FIRMWARE_IMAGE_HARDWARE_NUCLEO_U083RC 0x00830001UL
#define FIRMWARE_IMAGE_STM32_ROM_LOAD_ADDRESS 0x08000000UL
#define FIRMWARE_IMAGE_SIGNED_FORMAT_MAJOR 2U
#define FIRMWARE_IMAGE_SIGNED_FORMAT_MINOR 0U
#define FIRMWARE_IMAGE_SIGNED_HEADER_SIZE 512U
#define FIRMWARE_IMAGE_SIGNED_PREFIX_SIZE 80U
#define FIRMWARE_IMAGE_SIGNATURE_SIZE 64U
#define FIRMWARE_IMAGE_SIGNED_FLAG_AUTHENTICATED 0x0001U
#define FIRMWARE_IMAGE_SIGNATURE_ECDSA_P256_SHA256 1U
#define FIRMWARE_IMAGE_SLOT_0 0U
#define FIRMWARE_IMAGE_SLOT_1 1U
#define FIRMWARE_IMAGE_STM32_SLOT0_BASE 0x08010000UL
#define FIRMWARE_IMAGE_STM32_SLOT0_VECTOR_ADDRESS 0x08010200UL
#define FIRMWARE_IMAGE_STM32_SLOT1_BASE 0x08026000UL
#define FIRMWARE_IMAGE_STM32_SLOT1_VECTOR_ADDRESS 0x08026200UL
#define FIRMWARE_IMAGE_STM32_SLOT_SIZE 0x00016000UL
#define FIRMWARE_IMAGE_STM32_MAX_PAYLOAD_SIZE                                             \
    (FIRMWARE_IMAGE_STM32_SLOT_SIZE - FIRMWARE_IMAGE_SIGNED_HEADER_SIZE)

typedef enum {
    FIRMWARE_IMAGE_STATUS_OK = 0,
    FIRMWARE_IMAGE_STATUS_INVALID_ARGUMENT,
    FIRMWARE_IMAGE_STATUS_TRUNCATED,
    FIRMWARE_IMAGE_STATUS_BAD_MAGIC,
    FIRMWARE_IMAGE_STATUS_UNSUPPORTED_FORMAT,
    FIRMWARE_IMAGE_STATUS_INVALID_HEADER,
    FIRMWARE_IMAGE_STATUS_SIZE_MISMATCH,
    FIRMWARE_IMAGE_STATUS_INCOMPATIBLE,
    FIRMWARE_IMAGE_STATUS_HASH_MISMATCH,
    FIRMWARE_IMAGE_STATUS_UNSUPPORTED_ALGORITHM,
    FIRMWARE_IMAGE_STATUS_UNKNOWN_KEY,
    FIRMWARE_IMAGE_STATUS_INVALID_SLOT,
    FIRMWARE_IMAGE_STATUS_SIGNATURE_MISMATCH
} FirmwareImage_Status;

typedef struct {
    uint32_t target_id;
    uint32_t hardware_id;
    uint16_t version_major;
    uint16_t version_minor;
    uint16_t version_patch;
    uint16_t flags;
    uint32_t build_id;
    uint32_t load_address;
    uint32_t payload_size;
    uint8_t required_wire_major;
    uint8_t required_wire_minor;
    uint16_t required_management_major;
    uint16_t required_management_minor;
    uint8_t payload_sha256[FIRMWARE_IMAGE_SHA256_SIZE];
} FirmwareImage_Manifest;

typedef struct {
    uint32_t target_id;
    uint32_t hardware_id;
    uint32_t load_address;
    uint32_t maximum_payload_size;
    uint8_t wire_major;
    uint8_t wire_minor;
    uint16_t management_major;
    uint16_t management_minor;
} FirmwareImage_Compatibility;

typedef struct {
    FirmwareImage_Manifest manifest;
    uint8_t slot_id;
    uint8_t signature_algorithm;
    uint32_t key_id;
    uint8_t signature[FIRMWARE_IMAGE_SIGNATURE_SIZE];
} FirmwareImage_SignedManifest;

typedef struct {
    uint32_t target_id;
    uint32_t hardware_id;
    uint8_t slot_id;
    uint32_t vector_address;
    uint32_t maximum_payload_size;
    uint8_t signature_algorithm;
    uint32_t key_id;
    uint8_t wire_major;
    uint8_t wire_minor;
    uint16_t management_major;
    uint16_t management_minor;
} FirmwareImage_SignedCompatibility;

FirmwareImage_Status FirmwareImage_ParseManifest(const uint8_t *package, size_t package_size,
                                                 FirmwareImage_Manifest *manifest,
                                                 const uint8_t **payload);
FirmwareImage_Status
FirmwareImage_ValidateCompatibility(const FirmwareImage_Manifest *manifest,
                                    const FirmwareImage_Compatibility *compatibility);
FirmwareImage_Status
FirmwareImage_ValidateHash(const FirmwareImage_Manifest *manifest,
                           const uint8_t calculated_sha256[FIRMWARE_IMAGE_SHA256_SIZE]);
FirmwareImage_Status FirmwareImage_ParseSignedHeader(const uint8_t *image, size_t image_size,
                                                      FirmwareImage_SignedManifest *manifest,
                                                      const uint8_t **payload);
FirmwareImage_Status FirmwareImage_ValidateSignedCompatibility(
    const FirmwareImage_SignedManifest *manifest,
    const FirmwareImage_SignedCompatibility *compatibility);
FirmwareImage_Status FirmwareImage_GetCanonicalSignedBytes(const uint8_t *image, size_t image_size,
                                                            const uint8_t **canonical,
                                                            size_t *canonical_size);

#endif
