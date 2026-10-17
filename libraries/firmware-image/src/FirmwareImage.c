#include "firmware_image/FirmwareImage.h"

#include <limits.h>
#include <string.h>

static uint16_t FirmwareImage_ReadU16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8U));
}

static uint32_t FirmwareImage_ReadU32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) | ((uint32_t)data[2] << 16U) |
           ((uint32_t)data[3] << 24U);
}

static bool FirmwareImage_BytesAreZero(const uint8_t *data, size_t size) {
    uint8_t combined = 0U;
    size_t index;

    for (index = 0U; index < size; ++index) {
        combined |= data[index];
    }
    return combined == 0U;
}

FirmwareImage_Status FirmwareImage_ParseManifest(const uint8_t *package, size_t package_size,
                                                 FirmwareImage_Manifest *manifest,
                                                 const uint8_t **payload) {
    uint32_t payload_size;

    if ((package == NULL) || (manifest == NULL) || (payload == NULL)) {
        return FIRMWARE_IMAGE_STATUS_INVALID_ARGUMENT;
    }
    *payload = NULL;
    if (package_size < FIRMWARE_IMAGE_MANIFEST_SIZE) {
        return FIRMWARE_IMAGE_STATUS_TRUNCATED;
    }
    if ((package[0] != (uint8_t)FIRMWARE_IMAGE_MAGIC_0) ||
        (package[1] != (uint8_t)FIRMWARE_IMAGE_MAGIC_1) ||
        (package[2] != (uint8_t)FIRMWARE_IMAGE_MAGIC_2) ||
        (package[3] != (uint8_t)FIRMWARE_IMAGE_MAGIC_3)) {
        return FIRMWARE_IMAGE_STATUS_BAD_MAGIC;
    }
    if ((package[4] != FIRMWARE_IMAGE_FORMAT_MAJOR) ||
        (package[5] != FIRMWARE_IMAGE_FORMAT_MINOR)) {
        return FIRMWARE_IMAGE_STATUS_UNSUPPORTED_FORMAT;
    }
    if ((FirmwareImage_ReadU16(&package[6]) != FIRMWARE_IMAGE_MANIFEST_SIZE) ||
        (FirmwareImage_ReadU16(&package[22]) != 0U) ||
        !FirmwareImage_BytesAreZero(&package[42], 6U)) {
        return FIRMWARE_IMAGE_STATUS_INVALID_HEADER;
    }
    payload_size = FirmwareImage_ReadU32(&package[32]);
    if ((size_t)payload_size != (package_size - FIRMWARE_IMAGE_MANIFEST_SIZE)) {
        return FIRMWARE_IMAGE_STATUS_SIZE_MISMATCH;
    }

    manifest->target_id = FirmwareImage_ReadU32(&package[8]);
    manifest->hardware_id = FirmwareImage_ReadU32(&package[12]);
    manifest->version_major = FirmwareImage_ReadU16(&package[16]);
    manifest->version_minor = FirmwareImage_ReadU16(&package[18]);
    manifest->version_patch = FirmwareImage_ReadU16(&package[20]);
    manifest->flags = FirmwareImage_ReadU16(&package[22]);
    manifest->build_id = FirmwareImage_ReadU32(&package[24]);
    manifest->load_address = FirmwareImage_ReadU32(&package[28]);
    manifest->payload_size = payload_size;
    manifest->required_wire_major = package[36];
    manifest->required_wire_minor = package[37];
    manifest->required_management_major = FirmwareImage_ReadU16(&package[38]);
    manifest->required_management_minor = FirmwareImage_ReadU16(&package[40]);
    (void)memcpy(manifest->payload_sha256, &package[48], FIRMWARE_IMAGE_SHA256_SIZE);
    *payload = &package[FIRMWARE_IMAGE_MANIFEST_SIZE];
    return FIRMWARE_IMAGE_STATUS_OK;
}

FirmwareImage_Status
FirmwareImage_ValidateCompatibility(const FirmwareImage_Manifest *manifest,
                                    const FirmwareImage_Compatibility *compatibility) {
    if ((manifest == NULL) || (compatibility == NULL)) {
        return FIRMWARE_IMAGE_STATUS_INVALID_ARGUMENT;
    }
    if ((manifest->target_id != compatibility->target_id) ||
        (manifest->hardware_id != compatibility->hardware_id) ||
        (manifest->load_address != compatibility->load_address) ||
        (manifest->payload_size > compatibility->maximum_payload_size) ||
        (manifest->required_wire_major != compatibility->wire_major) ||
        (manifest->required_wire_minor > compatibility->wire_minor) ||
        (manifest->required_management_major != compatibility->management_major) ||
        (manifest->required_management_minor > compatibility->management_minor)) {
        return FIRMWARE_IMAGE_STATUS_INCOMPATIBLE;
    }
    return FIRMWARE_IMAGE_STATUS_OK;
}

FirmwareImage_Status
FirmwareImage_ValidateHash(const FirmwareImage_Manifest *manifest,
                           const uint8_t calculated_sha256[FIRMWARE_IMAGE_SHA256_SIZE]) {
    uint8_t difference = 0U;
    size_t index;

    if ((manifest == NULL) || (calculated_sha256 == NULL)) {
        return FIRMWARE_IMAGE_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0U; index < FIRMWARE_IMAGE_SHA256_SIZE; ++index) {
        difference |= (uint8_t)(manifest->payload_sha256[index] ^ calculated_sha256[index]);
    }
    return (difference == 0U) ? FIRMWARE_IMAGE_STATUS_OK : FIRMWARE_IMAGE_STATUS_HASH_MISMATCH;
}
