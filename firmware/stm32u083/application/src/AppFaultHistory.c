#include "AppFaultHistory.h"

#include <string.h>

#if defined(MULTI_MCU_RELIABILITY)
#include "diagnostics/DiagnosticsJournal.h"
#include "flash_layout.h"
#include "stm32u083_platform/PlatformSupervision.h"
#include "stm32u0xx_hal.h"

#define APP_FAULT_HISTORY_BUILD 0x00060001UL
#define APP_FAULT_HISTORY_BOOT_CODE 0x0200U
#define APP_FAULT_HISTORY_MAX_FAULTS 8U
#define APP_FAULT_HISTORY_MIN_INTERVAL_MS 1000U

static DiagnosticsJournal AppFaultHistory_Journal;
static uint32_t AppFaultHistory_Session;
static uint32_t AppFaultHistory_Written;
static uint32_t AppFaultHistory_LastTime;
static uint16_t AppFaultHistory_Codes[APP_FAULT_HISTORY_MAX_FAULTS];
static uint32_t AppFaultHistory_Details[APP_FAULT_HISTORY_MAX_FAULTS];

static uint32_t AppFaultHistory_Address(uint32_t page) {
    return page == 0U ? MULTI_MCU_FAULT_JOURNAL0_ORIGIN : MULTI_MCU_FAULT_JOURNAL1_ORIGIN;
}

static bool AppFaultHistory_ReadStorage(void *context, uint32_t page, uint32_t offset,
                                        uint8_t *data, size_t length) {
    (void)context;
    if ((page >= DIAGNOSTICS_JOURNAL_PAGE_COUNT) || (offset > DIAGNOSTICS_JOURNAL_PAGE_SIZE) ||
        (length > DIAGNOSTICS_JOURNAL_PAGE_SIZE - offset)) {
        return false;
    }
    (void)memcpy(data, (const void *)(uintptr_t)(AppFaultHistory_Address(page) + offset), length);
    return true;
}

static bool AppFaultHistory_ProgramStorage(void *context, uint32_t page, uint32_t offset,
                                           const uint8_t data[8]) {
    uint64_t doubleword;
    HAL_StatusTypeDef result;
    (void)context;
    if ((page >= DIAGNOSTICS_JOURNAL_PAGE_COUNT) || ((offset % 8U) != 0U) ||
        (offset > DIAGNOSTICS_JOURNAL_PAGE_SIZE - 8U)) {
        return false;
    }
    (void)memcpy(&doubleword, data, sizeof(doubleword));
    if (HAL_FLASH_Unlock() != HAL_OK) {
        return false;
    }
    result = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, AppFaultHistory_Address(page) + offset,
                               doubleword);
    (void)HAL_FLASH_Lock();
    return result == HAL_OK;
}

static bool AppFaultHistory_EraseStorage(void *context, uint32_t page) {
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t error = 0U;
    HAL_StatusTypeDef result;
    (void)context;
    if (page >= DIAGNOSTICS_JOURNAL_PAGE_COUNT) {
        return false;
    }
    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.Banks = FLASH_BANK_1;
    erase.Page = (AppFaultHistory_Address(page) - FLASH_BASE) / FLASH_PAGE_SIZE;
    erase.NbPages = 1U;
    if (HAL_FLASH_Unlock() != HAL_OK) {
        return false;
    }
    result = HAL_FLASHEx_Erase(&erase, &error);
    (void)HAL_FLASH_Lock();
    return result == HAL_OK;
}

static void AppFaultHistory_Write32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
    bytes[2] = (uint8_t)(value >> 16U);
    bytes[3] = (uint8_t)(value >> 24U);
}

static void AppFaultHistory_WriteHeader(NodeApi_ServiceResponse *response, bool found) {
    const DiagnosticsJournal_Status status = DiagnosticsJournal_GetStatus(&AppFaultHistory_Journal);
    (void)memset(response->data, 0, sizeof(response->data));
    response->data[0] = found ? 1U : 0U;
    response->data[1] = status.healthy ? 1U : 0U;
    AppFaultHistory_Write32(&response->data[4], status.overwritten);
    AppFaultHistory_Write32(&response->data[8], status.storage_errors);
    response->data_length = 12U;
}

void AppFaultHistory_Initialize(uint32_t session) {
    const DiagnosticsJournal_Storage storage = {NULL, AppFaultHistory_ReadStorage,
                                                AppFaultHistory_ProgramStorage,
                                                AppFaultHistory_EraseStorage};
    const Platform_ResetInfo reset = PlatformSupervision_GetResetInfo();
    DiagnosticsJournal_Record record = {0};
    AppFaultHistory_Session = session;
    AppFaultHistory_Written = 0U;
    AppFaultHistory_LastTime = 0U;
    if (DiagnosticsJournal_Initialize(&AppFaultHistory_Journal, &storage) !=
        DIAGNOSTICS_JOURNAL_OK) {
        return;
    }
    record.boot_count = reset.boot_count;
    record.session = session;
    record.build = APP_FAULT_HISTORY_BUILD;
    record.time_ms = HAL_GetTick();
    record.detail = reset.raw_flags;
    record.code = APP_FAULT_HISTORY_BOOT_CODE;
    record.reset_reason = (uint8_t)reset.reason;
    record.severity = DIAGNOSTICS_SEVERITY_INFO;
    /* Boot history is best-effort and never part of the self-test/boot gate. */
    (void)DiagnosticsJournal_Append(&AppFaultHistory_Journal, &record);
}

void AppFaultHistory_RecordFault(const Diagnostics_Fault *fault) {
    DiagnosticsJournal_Record record = {0};
    const Platform_ResetInfo reset = PlatformSupervision_GetResetInfo();
    uint32_t index;
    const uint32_t now = HAL_GetTick();
    if ((fault == NULL) || (fault->severity < DIAGNOSTICS_SEVERITY_WARNING) ||
        (AppFaultHistory_Written >= APP_FAULT_HISTORY_MAX_FAULTS)) {
        return;
    }
    for (index = 0U; index < AppFaultHistory_Written; ++index) {
        if ((AppFaultHistory_Codes[index] == fault->code) &&
            (AppFaultHistory_Details[index] == fault->detail)) {
            return;
        }
    }
    if ((AppFaultHistory_Written != 0U) &&
        ((now - AppFaultHistory_LastTime) < APP_FAULT_HISTORY_MIN_INTERVAL_MS) &&
        (fault->severity < DIAGNOSTICS_SEVERITY_FATAL)) {
        return;
    }
    record.boot_count = reset.boot_count;
    record.session = AppFaultHistory_Session;
    record.build = APP_FAULT_HISTORY_BUILD;
    record.time_ms = fault->timestamp_ms;
    record.correlation = fault->correlation_id;
    record.detail = fault->detail;
    record.code = fault->code;
    record.reset_reason = (uint8_t)reset.reason;
    record.severity = fault->severity;
    /* Reserve the wear budget even if programming fails. Storage failure does
     * not cause a retry/flash-write loop in normal application processing. */
    AppFaultHistory_Codes[AppFaultHistory_Written] = fault->code;
    AppFaultHistory_Details[AppFaultHistory_Written] = fault->detail;
    ++AppFaultHistory_Written;
    AppFaultHistory_LastTime = now;
    (void)DiagnosticsJournal_Append(&AppFaultHistory_Journal, &record);
}

NodeApi_Status AppFaultHistory_Read(uint32_t cursor, NodeApi_ServiceResponse *response) {
    DiagnosticsJournal_Record record = {0};
    bool found = false;
    if (response == NULL) {
        return NODE_API_STATUS_INVALID_ARGUMENT;
    }
    (void)DiagnosticsJournal_ReadAfter(&AppFaultHistory_Journal, cursor, &record, &found);
    AppFaultHistory_WriteHeader(response, found);
    if (found) {
        DiagnosticsJournal_EncodeRecord(&record, &response->data[12]);
    }
    response->data_length = 60U;
    return NODE_API_STATUS_OK;
}

NodeApi_Status AppFaultHistory_Clear(NodeApi_ServiceResponse *response) {
    DiagnosticsJournal_Result result;
    if (response == NULL) {
        return NODE_API_STATUS_INVALID_ARGUMENT;
    }
    result = DiagnosticsJournal_Clear(&AppFaultHistory_Journal);
    AppFaultHistory_WriteHeader(response, false);
    return result == DIAGNOSTICS_JOURNAL_OK ? NODE_API_STATUS_OK : NODE_API_STATUS_BUSY;
}

#else

void AppFaultHistory_Initialize(uint32_t session) { (void)session; }
void AppFaultHistory_RecordFault(const Diagnostics_Fault *fault) { (void)fault; }
NodeApi_Status AppFaultHistory_Read(uint32_t cursor, NodeApi_ServiceResponse *response) {
    (void)cursor;
    (void)response;
    return NODE_API_STATUS_UNSUPPORTED_COMMAND;
}
NodeApi_Status AppFaultHistory_Clear(NodeApi_ServiceResponse *response) {
    (void)response;
    return NODE_API_STATUS_UNSUPPORTED_COMMAND;
}

#endif
