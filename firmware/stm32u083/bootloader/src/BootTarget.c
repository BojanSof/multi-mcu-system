#include "stm32u083_bootloader/BootTarget.h"
#include "boot_protocol/BootProtocol.h"
#include "flash_layout.h"
#include "stm32u0xx_hal.h"
#include <string.h>
bool BootVerify_Slot(uint8_t slot, uint32_t expected_size);
static UART_HandleTypeDef BootTarget_Uart;
static bool BootTarget_ResetRequested;
static uint8_t BootTarget_WriteTail[8];
static uint8_t BootTarget_WriteTailLength;
static uint8_t BootTarget_WriteSlot = BOOT_PROTOCOL_NO_SLOT;
static uint32_t BootTarget_LogicalOffset;

/* HAL_Init enables the 1 kHz SysTick. Without this strong handler the CMSIS
 * startup's weak Default_Handler traps on the first tick, before recovery
 * UART traffic can be processed. */
void SysTick_Handler(void) { HAL_IncTick(); }
static uint8_t BootTarget_Prefetch[2];
static uint8_t BootTarget_PrefetchLength;
static uint32_t BootTarget_PageAddress(uint8_t page) {
    return page == 0U ? MULTI_MCU_METADATA_JOURNAL0_ORIGIN : MULTI_MCU_METADATA_JOURNAL1_ORIGIN;
}
static BootJournal_Result BootTarget_Read(void *c, uint8_t p, uint16_t o, uint8_t *d, size_t n) {
    (void)c;
    if (p >= 2U || o > BOOT_JOURNAL_PAGE_SIZE || n > BOOT_JOURNAL_PAGE_SIZE - o)
        return BOOT_JOURNAL_ERROR_IO;
    memcpy(d, (const void *)(uintptr_t)(BootTarget_PageAddress(p) + o), n);
    return BOOT_JOURNAL_OK;
}
static BootJournal_Result BootTarget_ProgramJournal(void *c, uint8_t p, uint16_t o,
                                                    const uint8_t d[8]) {
    (void)c;
    if (p >= 2U || (o & 7U) != 0U || o > BOOT_JOURNAL_PAGE_SIZE - 8U)
        return BOOT_JOURNAL_ERROR_IO;
    uint64_t value;
    memcpy(&value, d, 8U);
    HAL_FLASH_Unlock();
    HAL_StatusTypeDef s =
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, BootTarget_PageAddress(p) + o, value);
    HAL_FLASH_Lock();
    return s == HAL_OK ? BOOT_JOURNAL_OK : BOOT_JOURNAL_ERROR_IO;
}
static bool BootTarget_EraseAddress(uint32_t address, uint32_t size) {
    FLASH_EraseInitTypeDef e = {.TypeErase = FLASH_TYPEERASE_PAGES,
                                .Banks = FLASH_BANK_1,
                                .Page = (address - FLASH_BASE) / FLASH_PAGE_SIZE,
                                .NbPages = size / FLASH_PAGE_SIZE};
    uint32_t error;
    HAL_FLASH_Unlock();
    HAL_StatusTypeDef s = HAL_FLASHEx_Erase(&e, &error);
    HAL_FLASH_Lock();
    return s == HAL_OK;
}
static BootJournal_Result BootTarget_EraseJournal(void *c, uint8_t p) {
    (void)c;
    return BootTarget_EraseAddress(BootTarget_PageAddress(p), FLASH_PAGE_SIZE)
               ? BOOT_JOURNAL_OK
               : BOOT_JOURNAL_ERROR_IO;
}
BootJournal_Flash BootTarget_Journal(void) {
    BootJournal_Flash f = {NULL, BootTarget_Read, BootTarget_ProgramJournal,
                           BootTarget_EraseJournal};
    return f;
}
static BootProtocol_Status BootTarget_EraseSlot(void *c, uint8_t slot) {
    (void)c;
    if (slot >= 2U)
        return BOOT_PROTOCOL_STATUS_BOUNDARY;
    uint32_t base = slot == 0U ? MULTI_MCU_SLOT0_ORIGIN : MULTI_MCU_SLOT1_ORIGIN;
    if (!BootTarget_EraseAddress(base, MULTI_MCU_SLOT_SIZE))
        return BOOT_PROTOCOL_STATUS_FLASH;
    BootTarget_WriteSlot = slot;
    BootTarget_WriteTailLength = 0U;
    BootTarget_LogicalOffset = 0U;
    return BOOT_PROTOCOL_STATUS_OK;
}
static bool BootTarget_ProgramDoubleword(uint32_t address, const uint8_t data[8]) {
    uint64_t value;
    memcpy(&value, data, 8U);
    return HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, address, value) == HAL_OK;
}
static BootProtocol_Status BootTarget_Write(void *c, uint8_t slot, uint32_t offset,
                                            const uint8_t *data, size_t size) {
    (void)c;
    if (slot >= 2U || data == NULL || slot != BootTarget_WriteSlot ||
        offset != BootTarget_LogicalOffset || offset > MULTI_MCU_SLOT_SIZE ||
        size > (size_t)(MULTI_MCU_SLOT_SIZE - offset))
        return BOOT_PROTOCOL_STATUS_BOUNDARY;
    uint32_t base = slot == 0U ? MULTI_MCU_SLOT0_ORIGIN : MULTI_MCU_SLOT1_ORIGIN;
    HAL_FLASH_Unlock();
    for (size_t i = 0U; i < size; ++i) {
        BootTarget_WriteTail[BootTarget_WriteTailLength++] = data[i];
        if (BootTarget_WriteTailLength == 8U) {
            const uint32_t physical_offset = BootTarget_LogicalOffset + (uint32_t)i + 1U - 8U;
            if (!BootTarget_ProgramDoubleword(base + physical_offset, BootTarget_WriteTail)) {
                HAL_FLASH_Lock();
                return BOOT_PROTOCOL_STATUS_FLASH;
            }
            BootTarget_WriteTailLength = 0U;
        }
    }
    BootTarget_LogicalOffset += (uint32_t)size;
    HAL_FLASH_Lock();
    return BOOT_PROTOCOL_STATUS_OK;
}
static bool BootTarget_FlushTail(uint8_t slot) {
    if (BootTarget_WriteTailLength == 0U)
        return true;
    if (slot != BootTarget_WriteSlot || BootTarget_LogicalOffset < BootTarget_WriteTailLength)
        return false;
    for (uint8_t i = BootTarget_WriteTailLength; i < 8U; ++i)
        BootTarget_WriteTail[i] = 0xFFU;
    const uint32_t base = slot == 0U ? MULTI_MCU_SLOT0_ORIGIN : MULTI_MCU_SLOT1_ORIGIN;
    const uint32_t address = base + BootTarget_LogicalOffset - BootTarget_WriteTailLength;
    HAL_FLASH_Unlock();
    if (!BootTarget_ProgramDoubleword(address, BootTarget_WriteTail)) {
        HAL_FLASH_Lock();
        return false;
    }
    BootTarget_WriteTailLength = 0U;
    HAL_FLASH_Lock();
    return true;
}
static bool BootTarget_Validate(void *c, uint8_t slot, uint32_t size) {
    (void)c;
    if (size != 0U && !BootTarget_FlushTail(slot))
        return false;
    return BootVerify_Slot(slot, size);
}
static void BootTarget_Reboot(void *c) {
    (void)c;
    BootTarget_ResetRequested = true;
}
BootEngine_Storage BootTarget_Storage(void) {
    BootEngine_Storage s = {NULL, BootTarget_EraseSlot, BootTarget_Write, BootTarget_Validate,
                            BootTarget_Reboot};
    return s;
}
void BootTarget_Initialize(void) {
    HAL_Init();
    RCC_OscInitTypeDef o = {.OscillatorType = RCC_OSCILLATORTYPE_HSI,
                            .HSIState = RCC_HSI_ON,
                            .HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT};
    o.PLL.PLLState = RCC_PLL_NONE;
    (void)HAL_RCC_OscConfig(&o);
    RCC_ClkInitTypeDef k = {.ClockType =
                                RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1,
                            .SYSCLKSource = RCC_SYSCLKSOURCE_HSI,
                            .AHBCLKDivider = RCC_SYSCLK_DIV1,
                            .APB1CLKDivider = RCC_HCLK_DIV1};
    (void)HAL_RCC_ClockConfig(&k, FLASH_LATENCY_0);
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {.Pin = GPIO_PIN_9 | GPIO_PIN_10,
                             .Mode = GPIO_MODE_AF_PP,
                             .Pull = GPIO_NOPULL,
                             .Speed = GPIO_SPEED_FREQ_LOW,
                             .Alternate = GPIO_AF7_USART1};
    HAL_GPIO_Init(GPIOA, &gpio);
    __HAL_RCC_USART1_CLK_ENABLE();
    BootTarget_Uart.Instance = USART1;
    BootTarget_Uart.Init.BaudRate = 115200U;
    BootTarget_Uart.Init.WordLength = UART_WORDLENGTH_8B;
    BootTarget_Uart.Init.StopBits = UART_STOPBITS_1;
    BootTarget_Uart.Init.Parity = UART_PARITY_NONE;
    BootTarget_Uart.Init.Mode = UART_MODE_TX_RX;
    BootTarget_Uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    BootTarget_Uart.Init.OverSampling = UART_OVERSAMPLING_16;
    BootTarget_Uart.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    BootTarget_Uart.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    (void)HAL_UART_Init(&BootTarget_Uart);
}
uint8_t BootTarget_ValidSlotMask(void) {
    uint8_t m = 0U;
    for (uint8_t s = 0; s < 2U; s++)
        if (BootVerify_Slot(s, 0U))
            m |= (uint8_t)(1U << s);
    return m;
}
void BootTarget_ConsumeConfirmation(void) {
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
    __HAL_RCC_RTCAPB_CLK_ENABLE();
    uint32_t marker = TAMP->BKP0R;
    TAMP->BKP0R = 0U;
    if ((marker & 0x00FFFFFFUL) == BOOT_CONFIRM_APPLICATION_BASE) {
        uint8_t slot = (uint8_t)(marker >> BOOT_CONFIRM_SLOT_SHIFT);
        BootJournal_Flash f = BootTarget_Journal();
        BootJournal_Snapshot snapshot;
        (void)BootJournal_Confirm(&f, slot, &snapshot);
    }
}
void BootTarget_SetTrialMarker(bool trial, uint8_t slot) {
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
    __HAL_RCC_RTCAPB_CLK_ENABLE();
    TAMP->BKP1R = trial && slot < 2U ? BOOT_CONFIRM_MARKER(BOOT_CONFIRM_TRIAL_BASE, slot) : 0U;
}
bool BootTarget_Jump(uint8_t slot) {
    if (slot >= 2U)
        return false;
    uint32_t vector = slot == 0U ? MULTI_MCU_SLOT0_VECTOR_ORIGIN : MULTI_MCU_SLOT1_VECTOR_ORIGIN;
    uint32_t stack = *(const uint32_t *)(uintptr_t)vector;
    uint32_t reset = *(const uint32_t *)(uintptr_t)(vector + 4U);
    uint32_t reset_address = reset & ~1UL;
    uint32_t slot_end =
        (slot == 0U ? MULTI_MCU_SLOT0_ORIGIN : MULTI_MCU_SLOT1_ORIGIN) + MULTI_MCU_SLOT_SIZE;
    if (stack < SRAM1_BASE || stack > SRAM1_BASE + 0xA000UL || (stack & 7U) != 0U ||
        (reset & 1U) == 0U || reset_address < vector || reset_address >= slot_end)
        return false;
    __disable_irq();
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL = 0U;
    for (uint32_t i = 0; i < 1U; i++) {
        NVIC->ICER[i] = 0xFFFFFFFFUL;
        NVIC->ICPR[i] = 0xFFFFFFFFUL;
    }
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
    HAL_UART_DeInit(&BootTarget_Uart);
    HAL_DeInit();
    SCB->VTOR = vector;
    __DSB();
    __ISB();
    __set_MSP(stack);
    ((void (*)(void))(uintptr_t)reset)();
    return false;
}
bool BootTarget_RecoveryRequested(uint32_t timeout_ms) {
    const uint32_t start = HAL_GetTick();
    uint8_t previous = 0U;
    while ((HAL_GetTick() - start) < timeout_ms) {
        uint8_t byte;
        if (HAL_UART_Receive(&BootTarget_Uart, &byte, 1U, 1U) != HAL_OK)
            continue;
        if (previous == (uint8_t)BOOT_PROTOCOL_MAGIC_0 && byte == (uint8_t)BOOT_PROTOCOL_MAGIC_1) {
            BootTarget_Prefetch[0] = previous;
            BootTarget_Prefetch[1] = byte;
            BootTarget_PrefetchLength = 2U;
            return true;
        }
        previous = byte;
    }
    return false;
}
static void BootTarget_Resynchronize(uint8_t *frame, size_t *used) {
    size_t next = 1U;
    while (next + 1U < *used && (frame[next] != (uint8_t)BOOT_PROTOCOL_MAGIC_0 ||
                                 frame[next + 1U] != (uint8_t)BOOT_PROTOCOL_MAGIC_1))
        ++next;
    if (next + 1U < *used) {
        memmove(frame, frame + next, *used - next);
        *used -= next;
    } else if (*used > 0U && frame[*used - 1U] == (uint8_t)BOOT_PROTOCOL_MAGIC_0) {
        frame[0] = frame[*used - 1U];
        *used = 1U;
    } else {
        *used = 0U;
    }
}
void BootTarget_RunRecovery(uint8_t active_slot) {
    BootJournal_Flash journal = BootTarget_Journal();
    BootEngine_Storage storage = BootTarget_Storage();
    BootEngine_Context engine;
    BootEngine_Init(&engine, &journal, &storage);
    BootEngine_SetActiveSlot(&engine, active_slot);
    uint8_t frame[BOOT_PROTOCOL_MAX_FRAME_SIZE];
    size_t used = BootTarget_PrefetchLength;
    memcpy(frame, BootTarget_Prefetch, used);
    BootTarget_PrefetchLength = 0U;
    for (;;) {
        if (used == sizeof(frame))
            BootTarget_Resynchronize(frame, &used);
        if (HAL_UART_Receive(&BootTarget_Uart, &frame[used], 1U, 20U) != HAL_OK)
            continue;
        ++used;
        if (used >= 2U && (frame[0] != (uint8_t)BOOT_PROTOCOL_MAGIC_0 ||
                           frame[1] != (uint8_t)BOOT_PROTOCOL_MAGIC_1)) {
            BootTarget_Resynchronize(frame, &used);
            continue;
        }
        if (used < BOOT_PROTOCOL_HEADER_SIZE)
            continue;
        const uint16_t payload = (uint16_t)((uint16_t)frame[8] | ((uint16_t)frame[9] << 8U));
        if (payload > BOOT_PROTOCOL_MAX_PAYLOAD_SIZE) {
            BootTarget_Resynchronize(frame, &used);
            continue;
        }
        const size_t complete_size = BOOT_PROTOCOL_HEADER_SIZE + payload;
        if (used < complete_size)
            continue;
        BootProtocol_Message request, response;
        if (BootProtocol_Decode(frame, complete_size, &request) != BOOT_PROTOCOL_STATUS_OK) {
            BootTarget_Resynchronize(frame, &used);
            continue;
        }
        (void)BootEngine_Process(&engine, &request, &response);
        size_t output_size;
        if (BootProtocol_Encode(&response, frame, sizeof(frame), &output_size) ==
            BOOT_PROTOCOL_STATUS_OK)
            (void)HAL_UART_Transmit(&BootTarget_Uart, frame, (uint16_t)output_size, 1000U);
        if (BootTarget_ResetRequested)
            NVIC_SystemReset();
        used = 0U;
    }
}
