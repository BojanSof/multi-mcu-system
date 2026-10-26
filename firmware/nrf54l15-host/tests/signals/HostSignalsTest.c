#include <assert.h>
#include <errno.h>
#include <string.h>
#define MULTI_MCU_RELIABILITY 1
#include "../../src/platform/HostSignals.c"
struct device Test_Port;
static int Test_InterruptResult[11];
static bool Test_Ready = true;
static int Test_ConfigFailurePin = -1;
static unsigned Test_Configured;
static unsigned Test_Added;
static unsigned Test_Removed;
static unsigned Test_Warnings;
static unsigned Test_Wakes;
static int Test_WaitMs;
static bool Test_CallbackFailure;
static bool Test_IrqActive;

bool gpio_is_ready_dt(const struct gpio_dt_spec *pin) {
    (void)pin;
    return Test_Ready;
}
int gpio_pin_configure_dt(const struct gpio_dt_spec *pin, gpio_flags_t flags) {
    (void)flags;
    ++Test_Configured;
    return (int)pin->pin == Test_ConfigFailurePin ? -EIO : 0;
}
void gpio_init_callback(struct gpio_callback *callback,
                        void (*handler)(const struct device *, struct gpio_callback *,
                                        gpio_port_pins_t),
                        gpio_port_pins_t mask) {
    callback->handler = handler;
    callback->mask = mask;
}
int gpio_add_callback(const struct device *port, struct gpio_callback *callback) {
    (void)port;
    (void)callback;
    ++Test_Added;
    return Test_CallbackFailure ? -ENOSPC : 0;
}
int gpio_remove_callback(const struct device *port, struct gpio_callback *callback) {
    (void)port;
    (void)callback;
    ++Test_Removed;
    return 0;
}
int gpio_pin_interrupt_configure_dt(const struct gpio_dt_spec *pin, gpio_flags_t flags) {
    (void)flags;
    return Test_InterruptResult[pin->pin];
}
int gpio_pin_get_dt(const struct gpio_dt_spec *pin) { return pin->pin == 8U ? Test_IrqActive : 1; }
int gpio_pin_set_dt(const struct gpio_dt_spec *pin, int value) {
    (void)pin;
    (void)value;
    return 0;
}
void Test_Log(const char *format, ...) {
    (void)format;
    ++Test_Warnings;
}
void k_sem_give(struct k_sem *sem) {
    sem->tokens = 1U;
    ++Test_Wakes;
}
int k_sem_take(struct k_sem *sem, int milliseconds) {
    Test_WaitMs = milliseconds;
    if (sem->tokens != 0U) {
        --sem->tokens;
        return 0;
    }
    return -EAGAIN;
}
void k_msleep(int milliseconds) { Test_WaitMs = milliseconds; }
static void Test_Reset(void) {
    memset(Test_InterruptResult, 0, sizeof(Test_InterruptResult));
    Test_Configured = Test_Added = Test_Removed = Test_Warnings = Test_Wakes = 0U;
    Test_ConfigFailurePin = -1;
    Test_Ready = true;
    Test_CallbackFailure = Test_IrqActive = false;
    HostSignals_Event.tokens = 0U;
}
int main(void) {
    Test_Reset();
    /* Exact Port2 failure: optional interrupts must not abort main startup. */
    Test_InterruptResult[6] = Test_InterruptResult[8] = -ENOTSUP;
    assert(HostSignals_Initialize() == 0);
    assert(Test_Configured == 3U && Test_Removed == 2U && Test_Warnings == 2U);
    assert(!HostSignals_InterruptsEnabled());
    assert(HostSignals_NodeReady() && !HostSignals_NodeIrqAsserted());
    HostSignals_Wait();
    assert(Test_WaitMs == 10);
    HostSignals_Wake(); /* UART ISR remains able to wake the owner. */
    assert(HostSignals_Event.tokens == 1U);

    Test_Reset();
    Test_IrqActive = true;
    assert(HostSignals_Initialize() == 0 && HostSignals_InterruptsEnabled());
    assert(Test_Wakes == 1U); /* Initially asserted level is not missed. */
    HostSignals_ReadyCallback.handler(&Test_Port, &HostSignals_ReadyCallback, BIT(6));
    assert(Test_Wakes == 2U);

    Test_Reset();
    Test_InterruptResult[6] = -ENOTSUP;
    assert(HostSignals_Initialize() == 0 && !HostSignals_InterruptsEnabled());
    assert(Test_Removed == 1U && Test_Added == 2U); /* Try pins independently. */

    Test_Reset();
    Test_CallbackFailure = true;
    assert(HostSignals_Initialize() == 0 && !HostSignals_InterruptsEnabled());
    assert(Test_Warnings == 2U && Test_Removed == 0U);

    Test_Reset();
    Test_ConfigFailurePin = 6;
    assert(HostSignals_Initialize() == -EIO && Test_Added == 0U);
    Test_Reset();
    Test_Ready = false;
    assert(HostSignals_Initialize() == -ENODEV);
    return 0;
}
