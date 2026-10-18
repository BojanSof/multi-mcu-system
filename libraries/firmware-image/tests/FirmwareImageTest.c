#include "firmware_image/FirmwareImage.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            (void)fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #condition);    \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static void Test_WriteU16(uint8_t *data, uint16_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
}

static void Test_WriteU32(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

static void Test_MakePackage(uint8_t package[83]) {
    size_t index;

    (void)memset(package, 0, 83U);
    (void)memcpy(package, "MMFW", 4U);
    package[4] = FIRMWARE_IMAGE_FORMAT_MAJOR;
    package[5] = FIRMWARE_IMAGE_FORMAT_MINOR;
    Test_WriteU16(&package[6], FIRMWARE_IMAGE_MANIFEST_SIZE);
    Test_WriteU32(&package[8], FIRMWARE_IMAGE_TARGET_STM32U083_APPLICATION);
    Test_WriteU32(&package[12], FIRMWARE_IMAGE_HARDWARE_NUCLEO_U083RC);
    Test_WriteU16(&package[16], 1U);
    Test_WriteU16(&package[18], 2U);
    Test_WriteU16(&package[20], 3U);
    Test_WriteU32(&package[24], UINT32_C(0x12345678));
    Test_WriteU32(&package[28], FIRMWARE_IMAGE_STM32_ROM_LOAD_ADDRESS);
    Test_WriteU32(&package[32], 3U);
    package[36] = 1U;
    package[37] = 2U;
    Test_WriteU16(&package[38], 1U);
    Test_WriteU16(&package[40], 4U);
    for (index = 0U; index < FIRMWARE_IMAGE_SHA256_SIZE; ++index) {
        package[48U + index] = (uint8_t)index;
    }
    package[80] = 0xAAU;
    package[81] = 0xBBU;
    package[82] = 0xCCU;
}

static int Test_Parse(void) {
    uint8_t package[83];
    FirmwareImage_Manifest manifest;
    const uint8_t *payload = NULL;

    Test_MakePackage(package);
    CHECK(FirmwareImage_ParseManifest(package, sizeof(package), &manifest, &payload) ==
          FIRMWARE_IMAGE_STATUS_OK);
    CHECK(manifest.target_id == FIRMWARE_IMAGE_TARGET_STM32U083_APPLICATION);
    CHECK(manifest.hardware_id == FIRMWARE_IMAGE_HARDWARE_NUCLEO_U083RC);
    CHECK(manifest.version_major == 1U && manifest.version_minor == 2U &&
          manifest.version_patch == 3U);
    CHECK(manifest.build_id == UINT32_C(0x12345678));
    CHECK(manifest.load_address == FIRMWARE_IMAGE_STM32_ROM_LOAD_ADDRESS);
    CHECK(manifest.payload_size == 3U && payload == &package[80] && payload[2] == 0xCCU);
    CHECK(manifest.required_wire_major == 1U && manifest.required_wire_minor == 2U);
    CHECK(manifest.required_management_major == 1U && manifest.required_management_minor == 4U);
    CHECK(manifest.payload_sha256[31] == 31U);
    return 0;
}

static int Test_Malformed(void) {
    uint8_t package[83];
    FirmwareImage_Manifest manifest;
    const uint8_t *payload = NULL;

    Test_MakePackage(package);
    CHECK(FirmwareImage_ParseManifest(NULL, sizeof(package), &manifest, &payload) ==
          FIRMWARE_IMAGE_STATUS_INVALID_ARGUMENT);
    CHECK(FirmwareImage_ParseManifest(package, 79U, &manifest, &payload) ==
          FIRMWARE_IMAGE_STATUS_TRUNCATED);
    package[0] = 'X';
    CHECK(FirmwareImage_ParseManifest(package, sizeof(package), &manifest, &payload) ==
          FIRMWARE_IMAGE_STATUS_BAD_MAGIC);
    Test_MakePackage(package);
    package[5] = 1U;
    CHECK(FirmwareImage_ParseManifest(package, sizeof(package), &manifest, &payload) ==
          FIRMWARE_IMAGE_STATUS_UNSUPPORTED_FORMAT);
    Test_MakePackage(package);
    Test_WriteU16(&package[6], 81U);
    CHECK(FirmwareImage_ParseManifest(package, sizeof(package), &manifest, &payload) ==
          FIRMWARE_IMAGE_STATUS_INVALID_HEADER);
    Test_MakePackage(package);
    package[22] = 1U;
    CHECK(FirmwareImage_ParseManifest(package, sizeof(package), &manifest, &payload) ==
          FIRMWARE_IMAGE_STATUS_INVALID_HEADER);
    Test_MakePackage(package);
    package[47] = 1U;
    CHECK(FirmwareImage_ParseManifest(package, sizeof(package), &manifest, &payload) ==
          FIRMWARE_IMAGE_STATUS_INVALID_HEADER);
    Test_MakePackage(package);
    CHECK(FirmwareImage_ParseManifest(package, 82U, &manifest, &payload) ==
          FIRMWARE_IMAGE_STATUS_SIZE_MISMATCH);
    Test_WriteU32(&package[32], UINT32_MAX);
    CHECK(FirmwareImage_ParseManifest(package, sizeof(package), &manifest, &payload) ==
          FIRMWARE_IMAGE_STATUS_SIZE_MISMATCH);
    return 0;
}

static int Test_CompatibilityAndHash(void) {
    uint8_t package[83];
    uint8_t hash[FIRMWARE_IMAGE_SHA256_SIZE];
    FirmwareImage_Manifest manifest;
    FirmwareImage_Compatibility compatibility = {
        .target_id = FIRMWARE_IMAGE_TARGET_STM32U083_APPLICATION,
        .hardware_id = FIRMWARE_IMAGE_HARDWARE_NUCLEO_U083RC,
        .load_address = FIRMWARE_IMAGE_STM32_ROM_LOAD_ADDRESS,
        .maximum_payload_size = 3U,
        .wire_major = 1U,
        .wire_minor = 2U,
        .management_major = 1U,
        .management_minor = 4U,
    };
    const uint8_t *payload = NULL;

    Test_MakePackage(package);
    CHECK(FirmwareImage_ParseManifest(package, sizeof(package), &manifest, &payload) ==
          FIRMWARE_IMAGE_STATUS_OK);
    CHECK(FirmwareImage_ValidateCompatibility(&manifest, &compatibility) ==
          FIRMWARE_IMAGE_STATUS_OK);
    compatibility.wire_minor = 1U;
    CHECK(FirmwareImage_ValidateCompatibility(&manifest, &compatibility) ==
          FIRMWARE_IMAGE_STATUS_INCOMPATIBLE);
    compatibility.wire_minor = 2U;
    compatibility.target_id = FIRMWARE_IMAGE_TARGET_NRF54L15_HOST;
    CHECK(FirmwareImage_ValidateCompatibility(&manifest, &compatibility) ==
          FIRMWARE_IMAGE_STATUS_INCOMPATIBLE);
    compatibility.target_id = FIRMWARE_IMAGE_TARGET_STM32U083_APPLICATION;
    compatibility.hardware_id = UINT32_C(0xDEADBEEF);
    CHECK(FirmwareImage_ValidateCompatibility(&manifest, &compatibility) ==
          FIRMWARE_IMAGE_STATUS_INCOMPATIBLE);
    compatibility.hardware_id = FIRMWARE_IMAGE_HARDWARE_NUCLEO_U083RC;
    compatibility.load_address = UINT32_C(0x12345678);
    CHECK(FirmwareImage_ValidateCompatibility(&manifest, &compatibility) ==
          FIRMWARE_IMAGE_STATUS_INCOMPATIBLE);
    compatibility.load_address = FIRMWARE_IMAGE_STM32_ROM_LOAD_ADDRESS;
    compatibility.management_major = 2U;
    CHECK(FirmwareImage_ValidateCompatibility(&manifest, &compatibility) ==
          FIRMWARE_IMAGE_STATUS_INCOMPATIBLE);
    compatibility.management_major = 1U;
    compatibility.maximum_payload_size = 2U;
    CHECK(FirmwareImage_ValidateCompatibility(&manifest, &compatibility) ==
          FIRMWARE_IMAGE_STATUS_INCOMPATIBLE);
    (void)memcpy(hash, manifest.payload_sha256, sizeof(hash));
    CHECK(FirmwareImage_ValidateHash(&manifest, hash) == FIRMWARE_IMAGE_STATUS_OK);
    hash[0] ^= 1U;
    CHECK(FirmwareImage_ValidateHash(&manifest, hash) == FIRMWARE_IMAGE_STATUS_HASH_MISMATCH);
    CHECK(FirmwareImage_ValidateHash(NULL, hash) == FIRMWARE_IMAGE_STATUS_INVALID_ARGUMENT);
    return 0;
}

static void Test_MakeSignedImage(uint8_t image[515]) {
    size_t index;

    (void)memset(image, 0, 515U);
    (void)memcpy(image, "MMFW", 4U);
    image[4] = FIRMWARE_IMAGE_SIGNED_FORMAT_MAJOR;
    image[5] = FIRMWARE_IMAGE_SIGNED_FORMAT_MINOR;
    Test_WriteU16(&image[6], FIRMWARE_IMAGE_SIGNED_HEADER_SIZE);
    Test_WriteU32(&image[8], FIRMWARE_IMAGE_TARGET_STM32U083_APPLICATION);
    Test_WriteU32(&image[12], FIRMWARE_IMAGE_HARDWARE_NUCLEO_U083RC);
    Test_WriteU16(&image[16], 2U);
    Test_WriteU16(&image[18], 1U);
    Test_WriteU16(&image[20], 7U);
    Test_WriteU16(&image[22], FIRMWARE_IMAGE_SIGNED_FLAG_AUTHENTICATED);
    Test_WriteU32(&image[24], UINT32_C(0x87654321));
    Test_WriteU32(&image[28], FIRMWARE_IMAGE_STM32_SLOT1_VECTOR_ADDRESS);
    Test_WriteU32(&image[32], 3U);
    image[36] = 1U;
    image[37] = 2U;
    Test_WriteU16(&image[38], 1U);
    Test_WriteU16(&image[40], 4U);
    image[42] = FIRMWARE_IMAGE_SLOT_1;
    image[43] = FIRMWARE_IMAGE_SIGNATURE_ECDSA_P256_SHA256;
    Test_WriteU32(&image[44], 1U);
    for (index = 0U; index < FIRMWARE_IMAGE_SHA256_SIZE; ++index) {
        image[48U + index] = (uint8_t)(0x80U + index);
    }
    for (index = 0U; index < FIRMWARE_IMAGE_SIGNATURE_SIZE; ++index) {
        image[80U + index] = (uint8_t)(index + 1U);
    }
    image[512] = 0xAAU;
    image[513] = 0xBBU;
    image[514] = 0xCCU;
}

static int Test_SignedImage(void) {
    uint8_t image[515];
    FirmwareImage_SignedManifest manifest;
    FirmwareImage_SignedCompatibility compatibility = {
        .target_id = FIRMWARE_IMAGE_TARGET_STM32U083_APPLICATION,
        .hardware_id = FIRMWARE_IMAGE_HARDWARE_NUCLEO_U083RC,
        .slot_id = FIRMWARE_IMAGE_SLOT_1,
        .vector_address = FIRMWARE_IMAGE_STM32_SLOT1_VECTOR_ADDRESS,
        .maximum_payload_size = FIRMWARE_IMAGE_STM32_MAX_PAYLOAD_SIZE,
        .signature_algorithm = FIRMWARE_IMAGE_SIGNATURE_ECDSA_P256_SHA256,
        .key_id = 1U,
        .wire_major = 1U,
        .wire_minor = 2U,
        .management_major = 1U,
        .management_minor = 4U,
    };
    const uint8_t *payload = NULL;
    const uint8_t *canonical = NULL;
    size_t canonical_size = 0U;

    Test_MakeSignedImage(image);
    CHECK(FirmwareImage_ParseSignedHeader(image, sizeof(image), &manifest, &payload) ==
          FIRMWARE_IMAGE_STATUS_OK);
    CHECK(payload == &image[512] && manifest.manifest.payload_size == 3U);
    CHECK(manifest.slot_id == FIRMWARE_IMAGE_SLOT_1 && manifest.key_id == 1U);
    CHECK(manifest.manifest.load_address == FIRMWARE_IMAGE_STM32_SLOT1_VECTOR_ADDRESS);
    CHECK(manifest.signature[0] == 1U && manifest.signature[63] == 64U);
    CHECK(FirmwareImage_ValidateSignedCompatibility(&manifest, &compatibility) ==
          FIRMWARE_IMAGE_STATUS_OK);
    CHECK(FirmwareImage_GetCanonicalSignedBytes(image, sizeof(image), &canonical,
                                                &canonical_size) == FIRMWARE_IMAGE_STATUS_OK);
    CHECK(canonical == image && canonical_size == 80U);

    compatibility.key_id = 2U;
    CHECK(FirmwareImage_ValidateSignedCompatibility(&manifest, &compatibility) ==
          FIRMWARE_IMAGE_STATUS_UNKNOWN_KEY);
    compatibility.key_id = 1U;
    compatibility.slot_id = FIRMWARE_IMAGE_SLOT_0;
    CHECK(FirmwareImage_ValidateSignedCompatibility(&manifest, &compatibility) ==
          FIRMWARE_IMAGE_STATUS_INVALID_SLOT);
    compatibility.slot_id = FIRMWARE_IMAGE_SLOT_1;
    compatibility.vector_address = FIRMWARE_IMAGE_STM32_SLOT0_VECTOR_ADDRESS;
    CHECK(FirmwareImage_ValidateSignedCompatibility(&manifest, &compatibility) ==
          FIRMWARE_IMAGE_STATUS_INCOMPATIBLE);
    return 0;
}

static int Test_SignedMalformed(void) {
    uint8_t image[515];
    FirmwareImage_SignedManifest manifest;
    const uint8_t *payload = NULL;

    Test_MakeSignedImage(image);
    CHECK(FirmwareImage_ParseSignedHeader(image, 511U, &manifest, &payload) ==
          FIRMWARE_IMAGE_STATUS_TRUNCATED);
    image[42] = 2U;
    CHECK(FirmwareImage_ParseSignedHeader(image, sizeof(image), &manifest, &payload) ==
          FIRMWARE_IMAGE_STATUS_INVALID_SLOT);
    Test_MakeSignedImage(image);
    image[43] = 2U;
    CHECK(FirmwareImage_ParseSignedHeader(image, sizeof(image), &manifest, &payload) ==
          FIRMWARE_IMAGE_STATUS_UNSUPPORTED_ALGORITHM);
    Test_MakeSignedImage(image);
    image[144] = 1U;
    CHECK(FirmwareImage_ParseSignedHeader(image, sizeof(image), &manifest, &payload) ==
          FIRMWARE_IMAGE_STATUS_INVALID_HEADER);
    Test_MakeSignedImage(image);
    image[22] = 0U;
    CHECK(FirmwareImage_ParseSignedHeader(image, sizeof(image), &manifest, &payload) ==
          FIRMWARE_IMAGE_STATUS_INVALID_HEADER);
    Test_MakeSignedImage(image);
    (void)memset(&image[112], 0xFF, 32U);
    CHECK(FirmwareImage_ParseSignedHeader(image, sizeof(image), &manifest, &payload) ==
          FIRMWARE_IMAGE_STATUS_SIGNATURE_MISMATCH);
    Test_MakeSignedImage(image);
    (void)memset(&image[80], 0, 32U);
    CHECK(FirmwareImage_ParseSignedHeader(image, sizeof(image), &manifest, &payload) ==
          FIRMWARE_IMAGE_STATUS_SIGNATURE_MISMATCH);
    Test_MakeSignedImage(image);
    CHECK(FirmwareImage_ParseSignedHeader(image, sizeof(image) - 1U, &manifest, &payload) ==
          FIRMWARE_IMAGE_STATUS_SIZE_MISMATCH);
    return 0;
}

int main(void) {
    CHECK(Test_Parse() == 0);
    CHECK(Test_Malformed() == 0);
    CHECK(Test_CompatibilityAndHash() == 0);
    CHECK(Test_SignedImage() == 0);
    CHECK(Test_SignedMalformed() == 0);
    return 0;
}
