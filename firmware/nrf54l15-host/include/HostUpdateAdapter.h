#ifndef HOST_UPDATE_ADAPTER_H
#define HOST_UPDATE_ADAPTER_H
#include <stdbool.h>
#include <stdint.h>
struct smp_streamer;
void HostUpdateAdapter_Initialize(uint32_t boot_id);
void HostUpdateAdapter_Process(void);
/* Main-thread automatic reset; refuses queued or active update/repair ownership. */
int HostUpdateAdapter_TryRecoveryReset(void);
bool HostUpdateAdapter_IsBusy(void);
bool HostUpdateAdapter_ReserveRuntime(void);
void HostUpdateAdapter_SetRuntimeBusy(bool busy);
bool HostUpdateAdapter_UsesBootTransport(void);
bool HostUpdateAdapter_IsAvailable(void);
int HostUpdateAdapter_StatusRead(struct smp_streamer *);
int HostUpdateAdapter_ResetWrite(struct smp_streamer *);
int HostUpdateAdapter_UpdateWrite(struct smp_streamer *);
bool HostUpdateAdapter_RomAvailable(void);
int HostUpdateAdapter_RomStatusRead(struct smp_streamer *);
int HostUpdateAdapter_RomWrite(struct smp_streamer *);
#endif
