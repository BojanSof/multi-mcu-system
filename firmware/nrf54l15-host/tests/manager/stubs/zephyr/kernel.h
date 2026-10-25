#ifndef HOST_METRICS_TEST_KERNEL_H
#define HOST_METRICS_TEST_KERNEL_H
#include <stdint.h>
extern int64_t HostMetrics_TestClock;
static inline int64_t k_uptime_get(void) { return HostMetrics_TestClock; }
#define ARG_UNUSED(value) (void)(value)
#endif
