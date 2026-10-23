#ifndef STM32_ROM_BOOT_H
#define STM32_ROM_BOOT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define STM32_ROM_BOOT_BASE UINT32_C(0x08000000)
#define STM32_ROM_BOOT_SIZE UINT32_C(0x10000)
#define STM32_ROM_BOOT_PAGE_SIZE UINT32_C(0x800)
#define STM32_ROM_BOOT_DEVICE_ID UINT16_C(0x0489)
#define STM32_ROM_BOOT_CHUNK_SIZE 256U

typedef struct {
    void *context;
    int (*send)(void *, const uint8_t *, size_t);
    int (*read)(void *, uint8_t *, size_t, uint32_t timeout_ms);
} Stm32RomBoot_Ops;
typedef struct {
    Stm32RomBoot_Ops ops;
    uint16_t device_id;
    uint8_t version;
    bool connected;
    bool erased;
} Stm32RomBoot_Context;

void Stm32RomBoot_Initialize(Stm32RomBoot_Context *, const Stm32RomBoot_Ops *);
int Stm32RomBoot_Connect(Stm32RomBoot_Context *);
int Stm32RomBoot_EraseBootloader(Stm32RomBoot_Context *);
int Stm32RomBoot_Read(Stm32RomBoot_Context *, uint32_t offset, uint8_t *, size_t);
int Stm32RomBoot_WriteVerified(Stm32RomBoot_Context *, uint32_t offset, const uint8_t *, size_t);
#endif
