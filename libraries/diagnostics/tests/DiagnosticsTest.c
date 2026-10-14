#include "diagnostics/Diagnostics.h"

#include <limits.h>
#include <stdio.h>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            (void)fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #condition);    \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static Diagnostics_Fault Test_MakeFault(uint32_t correlation_id) {
    const Diagnostics_Fault fault = {
        .timestamp_ms = correlation_id * 10U,
        .correlation_id = correlation_id,
        .detail = UINT32_C(0xA5000000) | correlation_id,
        .code = (uint16_t)correlation_id,
        .source = DIAGNOSTICS_SOURCE_NODE,
        .severity = DIAGNOSTICS_SEVERITY_WARNING,
        .domain = DIAGNOSTICS_DOMAIN_SERVICE,
    };
    return fault;
}

static int Test_NullAndEmpty(void) {
    Diagnostics_State state;
    Diagnostics_Fault fault = Test_MakeFault(1U);

    Diagnostics_Initialize(NULL);
    Diagnostics_Initialize(&state);
    CHECK(Diagnostics_GetCount(NULL) == 0U);
    CHECK(!Diagnostics_RecordFault(NULL, &fault));
    CHECK(!Diagnostics_RecordFault(&state, NULL));
    CHECK(!Diagnostics_PopFault(NULL, &fault));
    CHECK(!Diagnostics_PopFault(&state, NULL));
    CHECK(!Diagnostics_PopFault(&state, &fault));
    Diagnostics_DisableAllInjections(NULL);
    CHECK(!Diagnostics_EnableInjection(NULL, DIAGNOSTICS_INJECTION_EARLY_READY));
    CHECK(!Diagnostics_IsInjectionEnabled(NULL, DIAGNOSTICS_INJECTION_EARLY_READY));
    CHECK(state.head == 0U && state.count == 0U && state.overflow_count == 0U &&
          state.total_recorded == 0U && state.injection_mask == 0U);
    return 0;
}

static int Test_FifoAndWrap(void) {
    Diagnostics_State state;
    Diagnostics_Fault fault;
    Diagnostics_Fault output;
    size_t index;

    Diagnostics_Initialize(&state);
    for (index = 0U; index < DIAGNOSTICS_RECORD_CAPACITY; ++index) {
        fault = Test_MakeFault((uint32_t)index + 1U);
        CHECK(Diagnostics_RecordFault(&state, &fault));
    }
    CHECK(Diagnostics_GetCount(&state) == DIAGNOSTICS_RECORD_CAPACITY);
    for (index = 0U; index < 5U; ++index) {
        CHECK(Diagnostics_PopFault(&state, &output));
        CHECK(output.correlation_id == (uint32_t)index + 1U);
    }
    for (index = 0U; index < 5U; ++index) {
        fault = Test_MakeFault((uint32_t)DIAGNOSTICS_RECORD_CAPACITY + (uint32_t)index + 1U);
        CHECK(Diagnostics_RecordFault(&state, &fault));
    }
    for (index = 5U; index < DIAGNOSTICS_RECORD_CAPACITY + 5U; ++index) {
        CHECK(Diagnostics_PopFault(&state, &output));
        CHECK(output.correlation_id == (uint32_t)index + 1U);
    }
    CHECK(Diagnostics_GetCount(&state) == 0U);
    CHECK(state.total_recorded == DIAGNOSTICS_RECORD_CAPACITY + 5U);
    CHECK(state.overflow_count == 0U);
    return 0;
}

static int Test_OverflowAndSaturation(void) {
    Diagnostics_State state;
    Diagnostics_Fault fault;
    Diagnostics_Fault output;
    size_t index;

    Diagnostics_Initialize(&state);
    for (index = 0U; index < DIAGNOSTICS_RECORD_CAPACITY + 3U; ++index) {
        fault = Test_MakeFault((uint32_t)index + 1U);
        CHECK(Diagnostics_RecordFault(&state, &fault));
    }
    CHECK(state.count == DIAGNOSTICS_RECORD_CAPACITY);
    CHECK(state.overflow_count == 3U);
    CHECK(state.total_recorded == DIAGNOSTICS_RECORD_CAPACITY + 3U);
    for (index = 3U; index < DIAGNOSTICS_RECORD_CAPACITY + 3U; ++index) {
        CHECK(Diagnostics_PopFault(&state, &output));
        CHECK(output.correlation_id == (uint32_t)index + 1U);
    }

    Diagnostics_Initialize(&state);
    state.total_recorded = UINT32_MAX;
    fault = Test_MakeFault(100U);
    CHECK(Diagnostics_RecordFault(&state, &fault));
    CHECK(state.total_recorded == UINT32_MAX);
    for (index = 1U; index < DIAGNOSTICS_RECORD_CAPACITY; ++index) {
        fault = Test_MakeFault((uint32_t)index + 100U);
        CHECK(Diagnostics_RecordFault(&state, &fault));
    }
    state.overflow_count = UINT32_MAX;
    fault = Test_MakeFault(999U);
    CHECK(Diagnostics_RecordFault(&state, &fault));
    CHECK(state.overflow_count == UINT32_MAX);
    CHECK(state.total_recorded == UINT32_MAX);
    return 0;
}

static int Test_MalformedFaultBoundaries(void) {
    Diagnostics_State state;
    Diagnostics_Fault fault = Test_MakeFault(1U);

    Diagnostics_Initialize(&state);
    fault.source = 0U;
    CHECK(!Diagnostics_RecordFault(&state, &fault));
    fault.source = (uint8_t)DIAGNOSTICS_SOURCE_TOOL + 1U;
    CHECK(!Diagnostics_RecordFault(&state, &fault));
    fault = Test_MakeFault(1U);
    fault.severity = 0U;
    CHECK(!Diagnostics_RecordFault(&state, &fault));
    fault.severity = (uint8_t)DIAGNOSTICS_SEVERITY_FATAL + 1U;
    CHECK(!Diagnostics_RecordFault(&state, &fault));
    fault = Test_MakeFault(1U);
    fault.domain = 0U;
    CHECK(!Diagnostics_RecordFault(&state, &fault));
    fault.domain = (uint8_t)DIAGNOSTICS_DOMAIN_RUNTIME + 1U;
    CHECK(!Diagnostics_RecordFault(&state, &fault));
    CHECK(state.count == 0U && state.total_recorded == 0U && state.overflow_count == 0U);
    return 0;
}

static int Test_Injections(void) {
    Diagnostics_State state;
    unsigned int value;

    Diagnostics_Initialize(&state);
    CHECK(!Diagnostics_EnableInjection(&state, (Diagnostics_InjectionId)0));
    CHECK(!Diagnostics_EnableInjection(
        &state, (Diagnostics_InjectionId)((unsigned int)DIAGNOSTICS_INJECTION_MAX + 1U)));
    CHECK(!Diagnostics_IsInjectionEnabled(&state, (Diagnostics_InjectionId)0));
    CHECK(!Diagnostics_IsInjectionEnabled(
        &state, (Diagnostics_InjectionId)((unsigned int)DIAGNOSTICS_INJECTION_MAX + 1U)));
    for (value = (unsigned int)DIAGNOSTICS_INJECTION_EARLY_READY;
         value <= (unsigned int)DIAGNOSTICS_INJECTION_MAX; ++value) {
        const Diagnostics_InjectionId injection = (Diagnostics_InjectionId)value;
        CHECK(Diagnostics_EnableInjection(&state, injection));
        CHECK(Diagnostics_IsInjectionEnabled(&state, injection));
    }
    CHECK(state.injection_mask == UINT32_C(0xFF));
    CHECK(Diagnostics_EnableInjection(&state, DIAGNOSTICS_INJECTION_EARLY_READY));
    CHECK(state.injection_mask == UINT32_C(0xFF));
    Diagnostics_DisableAllInjections(&state);
    CHECK(state.injection_mask == 0U);
    for (value = (unsigned int)DIAGNOSTICS_INJECTION_EARLY_READY;
         value <= (unsigned int)DIAGNOSTICS_INJECTION_MAX; ++value) {
        CHECK(!Diagnostics_IsInjectionEnabled(&state, (Diagnostics_InjectionId)value));
    }
    return 0;
}

int main(void) {
    CHECK(Test_NullAndEmpty() == 0);
    CHECK(Test_FifoAndWrap() == 0);
    CHECK(Test_OverflowAndSaturation() == 0);
    CHECK(Test_MalformedFaultBoundaries() == 0);
    CHECK(Test_Injections() == 0);
    return 0;
}
