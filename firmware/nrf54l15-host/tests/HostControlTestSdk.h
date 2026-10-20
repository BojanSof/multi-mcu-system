#ifndef HOST_CONTROL_TEST_SDK_H
#define HOST_CONTROL_TEST_SDK_H

/* Narrow SDK boundary double. Tests exercise production dispatch/state logic;
 * HOST_CONTROL_REAL_CBOR uses the pinned SDK's real CBOR encoder/decoder. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ARRAY_SIZE(value) (sizeof(value) / sizeof((value)[0]))
#define K_FOREVER 0
#define K_MUTEX_DEFINE(name) int name
static void k_mutex_lock(int *lock, int timeout) {
    (void)lock;
    (void)timeout;
}
static void k_mutex_unlock(int *lock) { (void)lock; }
static int64_t k_uptime_get(void) { return 1234; }
static uint32_t sys_rand32_get(void) { return 42U; }

#if defined(HOST_CONTROL_REAL_CBOR)
#include <zcbor_decode.h>
#include <zcbor_encode.h>

#include <mgmt/mcumgr/util/zcbor_bulk.h>
#else
typedef struct {
    int unused;
} zcbor_state_t;
struct TestStreamPart {
    zcbor_state_t *zs;
};
struct smp_streamer {
    struct TestStreamPart *reader;
    struct TestStreamPart *writer;
};
struct zcbor_string {
    const uint8_t *value;
    size_t len;
};
typedef bool zcbor_decoder_t(zcbor_state_t *, void *);
struct zcbor_map_decode_key_val {
    const char *key;
    void *value;
};
#define ZCBOR_MAP_DECODE_KEY_DECODER(key, decoder, value) {key, value}

static uint64_t Test_Request = 1U;
static uint32_t Test_Boot = 42U;
static uint32_t Test_Service = 0x1001U;
static uint32_t Test_Command = 1U;
static uint32_t Test_After;
static const uint8_t Test_Data[65] = {1U};
static size_t Test_Length;
static bool Test_Malformed;
static char Test_LastKey[32];
static char Test_Error[32];
static uint32_t Test_Operation;
static size_t Test_EventMaps;

static int zcbor_map_decode_bulk(zcbor_state_t *state, struct zcbor_map_decode_key_val *keys,
                                 size_t count, size_t *decoded) {
    (void)state;
    if (Test_Malformed) {
        return -1;
    }
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(keys[i].key, "request_id") == 0) {
            *(uint64_t *)keys[i].value = Test_Request;
        } else if (strcmp(keys[i].key, "expected_boot_id") == 0) {
            *(uint32_t *)keys[i].value = Test_Boot;
        } else if (strcmp(keys[i].key, "service") == 0) {
            *(uint32_t *)keys[i].value = Test_Service;
        } else if (strcmp(keys[i].key, "command") == 0) {
            *(uint32_t *)keys[i].value = Test_Command;
        } else if (strcmp(keys[i].key, "after") == 0) {
            *(uint32_t *)keys[i].value = Test_After;
        } else {
            *(struct zcbor_string *)keys[i].value = (struct zcbor_string){Test_Data, Test_Length};
        }
    }
    *decoded = count;
    return 0;
}
static bool zcbor_tstr_put_term(zcbor_state_t *state, const char *text, size_t max) {
    (void)state;
    (void)max;
    if (strcmp(Test_LastKey, "error") == 0) {
        strncpy(Test_Error, text, sizeof(Test_Error) - 1U);
        Test_LastKey[0] = 0;
    } else {
        strncpy(Test_LastKey, text, sizeof(Test_LastKey) - 1U);
    }
    return true;
}
#define zcbor_tstr_put_lit(state, text) zcbor_tstr_put_term(state, text, sizeof(text))
static bool zcbor_uint32_put(zcbor_state_t *state, uint32_t value) {
    (void)state;
    if (strcmp(Test_LastKey, "operation") == 0) {
        Test_Operation = value;
    }
    Test_LastKey[0] = 0;
    return true;
}
static bool zcbor_uint64_put(zcbor_state_t *state, uint64_t value) {
    (void)state;
    (void)value;
    Test_LastKey[0] = 0;
    return true;
}
static bool zcbor_bool_put(zcbor_state_t *state, bool value) {
    (void)state;
    (void)value;
    Test_LastKey[0] = 0;
    return true;
}
static bool zcbor_list_start_encode(zcbor_state_t *state, size_t count) {
    (void)state;
    (void)count;
    return true;
}
#define zcbor_list_end_encode zcbor_list_start_encode
static bool zcbor_map_start_encode(zcbor_state_t *state, size_t count) {
    (void)state;
    (void)count;
    Test_EventMaps++;
    return true;
}
#define zcbor_map_end_encode zcbor_list_start_encode
static bool zcbor_bstr_encode(zcbor_state_t *state, const struct zcbor_string *bytes) {
    (void)state;
    (void)bytes;
    return true;
}
#endif
#if defined(HOST_CONTROL_REAL_CBOR)
struct TestStreamPart {
    zcbor_state_t *zs;
};
struct smp_streamer {
    struct TestStreamPart *reader;
    struct TestStreamPart *writer;
};
#endif
#define MGMT_ERR_EOK 0
#define MGMT_ERR_EINVAL 3
#define MGMT_ERR_EMSGSIZE 7
struct mgmt_handler {
    int (*mh_read)(struct smp_streamer *);
    int (*mh_write)(struct smp_streamer *);
};
struct mgmt_group {
    const struct mgmt_handler *mg_handlers;
    size_t mg_handlers_count;
    unsigned mg_group_id;
};
static void mgmt_register_group(struct mgmt_group *group) { (void)group; }
#define MCUMGR_HANDLER_DEFINE(name, callback)

#endif
