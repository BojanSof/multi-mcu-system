#ifndef HOST_TRANSPORT_H
#define HOST_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t received_bytes;
    uint32_t transmitted_bytes;
    uint32_t receive_overflows;
    uint32_t transmit_overflows;
} HostTransport_Stats;

int HostTransport_Initialize(void);
bool HostTransport_IsAvailable(void);
size_t HostTransport_Read(uint8_t *data, size_t capacity);
int HostTransport_Write(const uint8_t *data, size_t length);
void HostTransport_Flush(void);
int HostTransport_ConfigureRom(bool enabled);
HostTransport_Stats HostTransport_GetStats(void);

#endif
