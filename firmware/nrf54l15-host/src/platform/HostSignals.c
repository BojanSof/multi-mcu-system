#include "HostSignals.h"

#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(host_signals, CONFIG_LOG_DEFAULT_LEVEL);

#define HOST_SIGNALS_NODE DT_PATH(zephyr_user)

static const struct gpio_dt_spec HostSignals_NodeReadyPin =
    GPIO_DT_SPEC_GET(HOST_SIGNALS_NODE, node_ready_gpios);
static const struct gpio_dt_spec HostSignals_NodeIrqPin =
    GPIO_DT_SPEC_GET(HOST_SIGNALS_NODE, node_irq_gpios);
static const struct gpio_dt_spec HostSignals_DebugPin =
    GPIO_DT_SPEC_GET(HOST_SIGNALS_NODE, host_debug_gpios);

#if defined(MULTI_MCU_RELIABILITY)
static K_SEM_DEFINE(HostSignals_Event, 0, 1);
static struct gpio_callback HostSignals_ReadyCallback;
static struct gpio_callback HostSignals_IrqCallback;
static bool HostSignals_IrqEnabled;
static bool HostSignals_ReadyEnabled;
static void HostSignals_Changed(const struct device *device, struct gpio_callback *callback,
                                gpio_port_pins_t pins) {
    (void)device;
    (void)callback;
    (void)pins;
    HostSignals_Wake();
}
static bool HostSignals_EnableInterrupt(const struct gpio_dt_spec *pin,
                                        struct gpio_callback *callback, gpio_flags_t flags,
                                        const char *name) {
    gpio_init_callback(callback, HostSignals_Changed, BIT(pin->pin));
    int result = gpio_add_callback(pin->port, callback);
    if (result == 0) {
        result = gpio_pin_interrupt_configure_dt(pin, flags);
        if (result != 0) {
            (void)gpio_remove_callback(pin->port, callback);
        }
    }
    if (result != 0) {
        /* Port2 has no GPIOTE instance in the pinned nRF54L15 driver. GPIO
         * reads still work: optional wake setup must not stop host startup. */
        LOG_WRN("%s GPIO interrupt unavailable (%d); using UART/poll fallback", name, result);
        return false;
    }
    return true;
}
#endif

bool HostSignals_InterruptsEnabled(void) {
#if defined(MULTI_MCU_RELIABILITY)
    return HostSignals_IrqEnabled && HostSignals_ReadyEnabled;
#else
    return false;
#endif
}

void HostSignals_Wake(void) {
#if defined(MULTI_MCU_RELIABILITY)
    k_sem_give(&HostSignals_Event);
#endif
}
void HostSignals_Wait(void) {
#if defined(MULTI_MCU_RELIABILITY)
    (void)k_sem_take(&HostSignals_Event, K_MSEC(10));
#else
    k_msleep(10);
#endif
}

int HostSignals_Initialize(void) {
    int result;

    if (!gpio_is_ready_dt(&HostSignals_NodeReadyPin) ||
        !gpio_is_ready_dt(&HostSignals_NodeIrqPin) || !gpio_is_ready_dt(&HostSignals_DebugPin)) {
        return -ENODEV;
    }
    result = gpio_pin_configure_dt(&HostSignals_NodeReadyPin, GPIO_INPUT);
    if (result != 0) {
        return result;
    }
    result = gpio_pin_configure_dt(&HostSignals_NodeIrqPin, GPIO_INPUT);
    if (result != 0) {
        return result;
    }
    result = gpio_pin_configure_dt(&HostSignals_DebugPin, GPIO_OUTPUT_INACTIVE);
    if (result != 0) {
        return result;
    }
#if defined(MULTI_MCU_RELIABILITY)
    HostSignals_ReadyEnabled = HostSignals_EnableInterrupt(
        &HostSignals_NodeReadyPin, &HostSignals_ReadyCallback, GPIO_INT_EDGE_BOTH, "READY");
    HostSignals_IrqEnabled = HostSignals_EnableInterrupt(
        &HostSignals_NodeIrqPin, &HostSignals_IrqCallback, GPIO_INT_EDGE_TO_ACTIVE, "IRQ");
    if (HostSignals_NodeIrqAsserted()) {
        HostSignals_Wake();
    }
#endif
    return 0;
}

bool HostSignals_NodeReady(void) { return gpio_pin_get_dt(&HostSignals_NodeReadyPin) > 0; }

bool HostSignals_NodeIrqAsserted(void) { return gpio_pin_get_dt(&HostSignals_NodeIrqPin) > 0; }

int HostSignals_SetDebug(bool active) {
    return gpio_pin_set_dt(&HostSignals_DebugPin, active ? 1 : 0);
}
