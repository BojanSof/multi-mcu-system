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

typedef enum {
    FIRMWARE_IMAGE_STATUS_OK = 0,
    FIRMWARE_IMAGE_STATUS_INVALID_ARGUMENT,
    FIRMWARE_IMAGE_STATUS_TRUNCATED,
    FIRMWARE_IMAGE_STATUS_BAD_MAGIC,
    FIRMWARE_IMAGE_STATUS_UNSUPPORTED_FORMAT,
    FIRMWARE_IMAGE_STATUS_INVALID_HEADER,
    FIRMWARE_IMAGE_STATUS_SIZE_MISMATCH,
    FIRMWARE_IMAGE_STATUS_INCOMPATIBLE,
    FIRMWARE_IMAGE_STATUS_HASH_MISMATCH
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

FirmwareImage_Status FirmwareImage_ParseManifest(const uint8_t *package, size_t package_size,
                                                 FirmwareImage_Manifest *manifest,
                                                 const uint8_t **payload);
FirmwareImage_Status
FirmwareImage_ValidateCompatibility(const FirmwareImage_Manifest *manifest,
                                    const FirmwareImage_Compatibility *compatibility);
FirmwareImage_Status
FirmwareImage_ValidateHash(const FirmwareImage_Manifest *manifest,
                           const uint8_t calculated_sha256[FIRMWARE_IMAGE_SHA256_SIZE]);

#endif
