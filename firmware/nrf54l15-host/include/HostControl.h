#ifndef HOST_CONTROL_H
#define HOST_CONTROL_H

/* Called only by the main thread; SMP handlers use snapshots and a bounded mailbox. */
void HostControl_Initialize(void);
void HostControl_Process(void);

#endif
