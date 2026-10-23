#ifndef HOST_STM_UPDATE_H
#define HOST_STM_UPDATE_H

#include "boot_protocol/BootProtocol.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    HOST_STM_UPDATE_IDLE = 0,
    HOST_STM_UPDATE_QUERYING,
    HOST_STM_UPDATE_ERASING,
    HOST_STM_UPDATE_READY,
    HOST_STM_UPDATE_WRITING,
    HOST_STM_UPDATE_FINALIZING,
    HOST_STM_UPDATE_PENDING,
    HOST_STM_UPDATE_BOOTING,
    HOST_STM_UPDATE_VERIFYING,
    HOST_STM_UPDATE_RETURNING,
    HOST_STM_UPDATE_COMPLETE,
    HOST_STM_UPDATE_ROLLED_BACK,
    HOST_STM_UPDATE_FAILED,
    HOST_STM_UPDATE_ABORTED
} HostStmUpdate_State;

typedef struct {
    void *context;
    int (*send)(void *, const uint8_t *, size_t);
    size_t (*read)(void *, uint8_t *, size_t);
    int64_t (*now)(void *);
    int (*reset)(void *);
    void (*runtime_restart)(void *);
    bool (*runtime_online)(void *);
} HostStmUpdate_Ops;

typedef struct {
    HostStmUpdate_State state;
    uint32_t offset;
    uint32_t total;
    uint8_t slot;
    uint8_t confirmed_slot;
    uint8_t boot_state;
    uint8_t protocol;
    int error;
    bool active;
} HostStmUpdate_Status;

typedef struct {
    HostStmUpdate_Ops ops;
    HostStmUpdate_Status status;
    uint8_t header[512];
    uint8_t tx[BOOT_PROTOCOL_MAX_FRAME_SIZE];
    uint8_t rx[BOOT_PROTOCOL_MAX_FRAME_SIZE * 2U];
    size_t tx_length;
    size_t rx_length;
    uint16_t sequence;
    uint16_t pending_sequence;
    uint16_t pending_length;
    uint8_t pending_command;
    uint8_t previous_confirmed_slot;
    uint8_t retries;
    int64_t deadline;
    int64_t activity;
    bool outstanding;
    bool reset_only;
    bool committed;
    bool boot_owned;
    HostStmUpdate_State outcome;
} HostStmUpdate_Context;

void HostStmUpdate_Initialize(HostStmUpdate_Context *, const HostStmUpdate_Ops *);
int HostStmUpdate_Begin(HostStmUpdate_Context *, const uint8_t header[512], uint32_t total);
int HostStmUpdate_Reset(HostStmUpdate_Context *);
int HostStmUpdate_Write(HostStmUpdate_Context *, uint32_t offset, const uint8_t *, size_t);
int HostStmUpdate_Finish(HostStmUpdate_Context *);
int HostStmUpdate_Abort(HostStmUpdate_Context *);
void HostStmUpdate_Process(HostStmUpdate_Context *);
bool HostStmUpdate_UsesBootTransport(const HostStmUpdate_Context *);
const char *HostStmUpdate_StateName(HostStmUpdate_State);

#endif
