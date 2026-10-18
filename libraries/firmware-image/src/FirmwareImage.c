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

static int FirmwareImage_CompareBigEndian(const uint8_t left[32], const uint8_t right[32]) {
    for (size_t index = 0U; index < 32U; ++index) {
        if (left[index] < right[index]) {
            return -1;
        }
        if (left[index] > right[index]) {
            return 1;
        }
    }
    return 0;
}

static bool FirmwareImage_P256SignatureIsCanonical(const uint8_t signature[64]) {
    static const uint8_t order[32] = {
        0xFFU, 0xFFU, 0xFFU, 0xFFU, 0x00U, 0x00U, 0x00U, 0x00U, 0xFFU, 0xFFU, 0xFFU,
        0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xBCU, 0xE6U, 0xFAU, 0xADU, 0xA7U, 0x17U,
        0x9EU, 0x84U, 0xF3U, 0xB9U, 0xCAU, 0xC2U, 0xFCU, 0x63U, 0x25U, 0x51U,
    };
    static const uint8_t half_order[32] = {
        0x7FU, 0xFFU, 0xFFU, 0xFFU, 0x80U, 0x00U, 0x00U, 0x00U, 0x7FU, 0xFFU, 0xFFU,
        0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xDEU, 0x73U, 0x7DU, 0x56U, 0xD3U, 0x8BU,
        0xCFU, 0x42U, 0x79U, 0xD2U, 0xE5U, 0x61U, 0x7EU, 0x31U, 0x92U, 0xA8U,
    };
    uint8_t r_nonzero = 0U;
    uint8_t s_nonzero = 0U;

    for (size_t index = 0U; index < 32U; ++index) {
        r_nonzero |= signature[index];
        s_nonzero |= signature[32U + index];
    }
    return r_nonzero != 0U && s_nonzero != 0U &&
           FirmwareImage_CompareBigEndian(signature, order) < 0 &&
           FirmwareImage_CompareBigEndian(&signature[32], order) < 0 &&
           FirmwareImage_CompareBigEndian(&signature[32], half_order) <= 0;
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

FirmwareImage_Status FirmwareImage_ParseSignedHeader(const uint8_t *image, size_t image_size,
                                                     FirmwareImage_SignedManifest *manifest,
                                                     const uint8_t **payload) {
    uint32_t payload_size;

    if ((image == NULL) || (manifest == NULL) || (payload == NULL)) {
        return FIRMWARE_IMAGE_STATUS_INVALID_ARGUMENT;
    }
    *payload = NULL;
    if (image_size < FIRMWARE_IMAGE_SIGNED_HEADER_SIZE) {
        return FIRMWARE_IMAGE_STATUS_TRUNCATED;
    }
    if ((image[0] != (uint8_t)FIRMWARE_IMAGE_MAGIC_0) ||
        (image[1] != (uint8_t)FIRMWARE_IMAGE_MAGIC_1) ||
        (image[2] != (uint8_t)FIRMWARE_IMAGE_MAGIC_2) ||
        (image[3] != (uint8_t)FIRMWARE_IMAGE_MAGIC_3)) {
        return FIRMWARE_IMAGE_STATUS_BAD_MAGIC;
    }
    if ((image[4] != FIRMWARE_IMAGE_SIGNED_FORMAT_MAJOR) ||
        (image[5] != FIRMWARE_IMAGE_SIGNED_FORMAT_MINOR)) {
        return FIRMWARE_IMAGE_STATUS_UNSUPPORTED_FORMAT;
    }
    if ((FirmwareImage_ReadU16(&image[6]) != FIRMWARE_IMAGE_SIGNED_HEADER_SIZE) ||
        (FirmwareImage_ReadU16(&image[22]) != FIRMWARE_IMAGE_SIGNED_FLAG_AUTHENTICATED) ||
        !FirmwareImage_BytesAreZero(&image[144], FIRMWARE_IMAGE_SIGNED_HEADER_SIZE - 144U)) {
        return FIRMWARE_IMAGE_STATUS_INVALID_HEADER;
    }
    if ((image[42] != FIRMWARE_IMAGE_SLOT_0) && (image[42] != FIRMWARE_IMAGE_SLOT_1)) {
        return FIRMWARE_IMAGE_STATUS_INVALID_SLOT;
    }
    if (image[43] != FIRMWARE_IMAGE_SIGNATURE_ECDSA_P256_SHA256) {
        return FIRMWARE_IMAGE_STATUS_UNSUPPORTED_ALGORITHM;
    }
    if (!FirmwareImage_P256SignatureIsCanonical(&image[80])) {
        return FIRMWARE_IMAGE_STATUS_SIGNATURE_MISMATCH;
    }
    payload_size = FirmwareImage_ReadU32(&image[32]);
    if ((size_t)payload_size != (image_size - FIRMWARE_IMAGE_SIGNED_HEADER_SIZE)) {
        return FIRMWARE_IMAGE_STATUS_SIZE_MISMATCH;
    }

    manifest->manifest.target_id = FirmwareImage_ReadU32(&image[8]);
    manifest->manifest.hardware_id = FirmwareImage_ReadU32(&image[12]);
    manifest->manifest.version_major = FirmwareImage_ReadU16(&image[16]);
    manifest->manifest.version_minor = FirmwareImage_ReadU16(&image[18]);
    manifest->manifest.version_patch = FirmwareImage_ReadU16(&image[20]);
    manifest->manifest.flags = FirmwareImage_ReadU16(&image[22]);
    manifest->manifest.build_id = FirmwareImage_ReadU32(&image[24]);
    manifest->manifest.load_address = FirmwareImage_ReadU32(&image[28]);
    manifest->manifest.payload_size = payload_size;
    manifest->manifest.required_wire_major = image[36];
    manifest->manifest.required_wire_minor = image[37];
    manifest->manifest.required_management_major = FirmwareImage_ReadU16(&image[38]);
    manifest->manifest.required_management_minor = FirmwareImage_ReadU16(&image[40]);
    (void)memcpy(manifest->manifest.payload_sha256, &image[48], FIRMWARE_IMAGE_SHA256_SIZE);
    manifest->slot_id = image[42];
    manifest->signature_algorithm = image[43];
    manifest->key_id = FirmwareImage_ReadU32(&image[44]);
    (void)memcpy(manifest->signature, &image[80], FIRMWARE_IMAGE_SIGNATURE_SIZE);
    *payload = &image[FIRMWARE_IMAGE_SIGNED_HEADER_SIZE];
    return FIRMWARE_IMAGE_STATUS_OK;
}

FirmwareImage_Status
FirmwareImage_ValidateSignedCompatibility(const FirmwareImage_SignedManifest *manifest,
                                          const FirmwareImage_SignedCompatibility *compatibility) {
    if ((manifest == NULL) || (compatibility == NULL)) {
        return FIRMWARE_IMAGE_STATUS_INVALID_ARGUMENT;
    }
    if (manifest->signature_algorithm != compatibility->signature_algorithm) {
        return FIRMWARE_IMAGE_STATUS_UNSUPPORTED_ALGORITHM;
    }
    if (manifest->key_id != compatibility->key_id) {
        return FIRMWARE_IMAGE_STATUS_UNKNOWN_KEY;
    }
    if (manifest->slot_id != compatibility->slot_id) {
        return FIRMWARE_IMAGE_STATUS_INVALID_SLOT;
    }
    if ((manifest->manifest.target_id != compatibility->target_id) ||
        (manifest->manifest.hardware_id != compatibility->hardware_id) ||
        (manifest->manifest.load_address != compatibility->vector_address) ||
        (manifest->manifest.payload_size > compatibility->maximum_payload_size) ||
        (manifest->manifest.required_wire_major != compatibility->wire_major) ||
        (manifest->manifest.required_wire_minor > compatibility->wire_minor) ||
        (manifest->manifest.required_management_major != compatibility->management_major) ||
        (manifest->manifest.required_management_minor > compatibility->management_minor)) {
        return FIRMWARE_IMAGE_STATUS_INCOMPATIBLE;
    }
    return FIRMWARE_IMAGE_STATUS_OK;
}

FirmwareImage_Status FirmwareImage_GetCanonicalSignedBytes(const uint8_t *image, size_t image_size,
                                                           const uint8_t **canonical,
                                                           size_t *canonical_size) {
    FirmwareImage_SignedManifest manifest;
    const uint8_t *payload;
    FirmwareImage_Status status;

    if ((canonical == NULL) || (canonical_size == NULL)) {
        return FIRMWARE_IMAGE_STATUS_INVALID_ARGUMENT;
    }
    *canonical = NULL;
    *canonical_size = 0U;
    status = FirmwareImage_ParseSignedHeader(image, image_size, &manifest, &payload);
    if (status != FIRMWARE_IMAGE_STATUS_OK) {
        return status;
    }
    (void)payload;
    *canonical = image;
    *canonical_size = FIRMWARE_IMAGE_SIGNED_PREFIX_SIZE;
    return FIRMWARE_IMAGE_STATUS_OK;
}
