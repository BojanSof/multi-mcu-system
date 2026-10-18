#ifndef HOST_BOOTLOADER_CLIENT_H
#define HOST_BOOTLOADER_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    HOST_BOOTLOADER_CLIENT_IDLE = 0,
    HOST_BOOTLOADER_CLIENT_QUERYING,
    HOST_BOOTLOADER_CLIENT_READY,
    HOST_BOOTLOADER_CLIENT_ERASING,
    HOST_BOOTLOADER_CLIENT_WRITING,
    HOST_BOOTLOADER_CLIENT_FINALIZING,
    HOST_BOOTLOADER_CLIENT_SETTING_PENDING,
    HOST_BOOTLOADER_CLIENT_VERIFYING_STATE,
    HOST_BOOTLOADER_CLIENT_COMPLETE,
    HOST_BOOTLOADER_CLIENT_REBOOTING,
    HOST_BOOTLOADER_CLIENT_FAILED
} HostBootloaderClient_State;

typedef struct {
    HostBootloaderClient_State state;
    uint32_t image_size;
    uint32_t bytes_written;
    uint16_t next_sequence;
    uint8_t target_slot;
    uint8_t last_bootloader_status;
    int last_error;
} HostBootloaderClient_Status;

void HostBootloaderClient_Initialize(void);
int HostBootloaderClient_Query(void);
int HostBootloaderClient_StartUpdate(const uint8_t *image, size_t image_size);
int HostBootloaderClient_Reboot(void);
void HostBootloaderClient_Process(void);
HostBootloaderClient_Status HostBootloaderClient_GetStatus(void);
const char *HostBootloaderClient_StateName(HostBootloaderClient_State state);

#endif
