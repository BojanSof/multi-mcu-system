#include "stm32u083_platform/PlatformSupervision.h"

#include <limits.h>

#include "stm32u0xx_hal.h"

#define PLATFORM_BOOT_MAGIC 0x42544D35UL
#define PLATFORM_RESET_FLAG_OPTION_BYTE (1U << 0)
#define PLATFORM_RESET_FLAG_PIN (1U << 1)
#define PLATFORM_RESET_FLAG_POWER (1U << 2)
#define PLATFORM_RESET_FLAG_SOFTWARE (1U << 3)
#define PLATFORM_RESET_FLAG_IWDG (1U << 4)
#define PLATFORM_RESET_FLAG_WWDG (1U << 5)
#define PLATFORM_RESET_FLAG_LOW_POWER (1U << 6)

/* Nominal 4.096 s at 32 kHz LSI; still over 2.6 s at the high LSI tolerance. */
#define PLATFORM_IWDG_RELOAD 4095U

typedef struct {
    uint32_t magic;
    uint32_t inverse_magic;
    uint32_t boot_count;
    uint32_t inverse_boot_count;
} Platform_PersistentBootState;

/* The linker emits this NOBITS orphan outside .bss, so startup does not clear it. */
static Platform_PersistentBootState PlatformSupervision_BootState
    __attribute__((section(".noinit")));
static Platform_ResetInfo PlatformSupervision_ResetInfo;
static IWDG_HandleTypeDef PlatformSupervision_Iwdg;

static uint16_t PlatformSupervision_NormalizeFlags(uint32_t csr) {
    uint16_t flags = 0U;
    flags |= (csr & RCC_CSR_OBLRSTF) != 0U ? PLATFORM_RESET_FLAG_OPTION_BYTE : 0U;
    flags |= (csr & RCC_CSR_PINRSTF) != 0U ? PLATFORM_RESET_FLAG_PIN : 0U;
    flags |= (csr & RCC_CSR_PWRRSTF) != 0U ? PLATFORM_RESET_FLAG_POWER : 0U;
    flags |= (csr & RCC_CSR_SFTRSTF) != 0U ? PLATFORM_RESET_FLAG_SOFTWARE : 0U;
    flags |= (csr & RCC_CSR_IWDGRSTF) != 0U ? PLATFORM_RESET_FLAG_IWDG : 0U;
    flags |= (csr & RCC_CSR_WWDGRSTF) != 0U ? PLATFORM_RESET_FLAG_WWDG : 0U;
    flags |= (csr & RCC_CSR_LPWRRSTF) != 0U ? PLATFORM_RESET_FLAG_LOW_POWER : 0U;
    return flags;
}

void PlatformSupervision_CaptureReset(void) {
    const uint32_t csr = RCC->CSR;
    const bool persistent_valid =
        (PlatformSupervision_BootState.magic == PLATFORM_BOOT_MAGIC) &&
        (PlatformSupervision_BootState.inverse_magic == ~PLATFORM_BOOT_MAGIC) &&
        (PlatformSupervision_BootState.inverse_boot_count ==
         ~PlatformSupervision_BootState.boot_count);

    if (!persistent_valid) {
        PlatformSupervision_BootState.boot_count = 0U;
    }
    if (PlatformSupervision_BootState.boot_count != UINT32_MAX) {
        ++PlatformSupervision_BootState.boot_count;
    }
    PlatformSupervision_BootState.magic = PLATFORM_BOOT_MAGIC;
    PlatformSupervision_BootState.inverse_magic = ~PLATFORM_BOOT_MAGIC;
    PlatformSupervision_BootState.inverse_boot_count = ~PlatformSupervision_BootState.boot_count;

    PlatformSupervision_ResetInfo.raw_flags = PlatformSupervision_NormalizeFlags(csr);
    PlatformSupervision_ResetInfo.boot_count = PlatformSupervision_BootState.boot_count;
    if ((csr & (RCC_CSR_IWDGRSTF | RCC_CSR_WWDGRSTF)) != 0U) {
        PlatformSupervision_ResetInfo.reason = PLATFORM_RESET_WATCHDOG;
    } else if ((csr & RCC_CSR_SFTRSTF) != 0U) {
        PlatformSupervision_ResetInfo.reason = PLATFORM_RESET_SOFTWARE;
    } else if ((csr & RCC_CSR_PINRSTF) != 0U) {
        PlatformSupervision_ResetInfo.reason = PLATFORM_RESET_PIN;
    } else if ((csr & RCC_CSR_PWRRSTF) != 0U) {
        PlatformSupervision_ResetInfo.reason = PLATFORM_RESET_POWER_ON;
    } else {
        PlatformSupervision_ResetInfo.reason = PLATFORM_RESET_UNKNOWN;
    }
    __HAL_RCC_CLEAR_RESET_FLAGS();
}

Platform_ResetInfo PlatformSupervision_GetResetInfo(void) { return PlatformSupervision_ResetInfo; }

Platform_Status PlatformSupervision_StartWatchdog(void) {
    PlatformSupervision_Iwdg.Instance = IWDG;
    PlatformSupervision_Iwdg.Init.Prescaler = IWDG_PRESCALER_32;
    PlatformSupervision_Iwdg.Init.Reload = PLATFORM_IWDG_RELOAD;
    PlatformSupervision_Iwdg.Init.Window = IWDG_WINDOW_DISABLE;
    PlatformSupervision_Iwdg.Init.EWI = IWDG_EWI_DISABLE;
    return HAL_IWDG_Init(&PlatformSupervision_Iwdg) == HAL_OK ? PLATFORM_STATUS_OK
                                                              : PLATFORM_STATUS_ERROR;
}

void PlatformSupervision_RefreshWatchdog(void) {
    (void)HAL_IWDG_Refresh(&PlatformSupervision_Iwdg);
}
