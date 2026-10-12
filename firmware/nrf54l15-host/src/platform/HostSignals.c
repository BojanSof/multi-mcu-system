#include "HostSignals.h"

#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>

#define HOST_SIGNALS_NODE DT_PATH(zephyr_user)

static const struct gpio_dt_spec HostSignals_NodeReadyPin =
    GPIO_DT_SPEC_GET(HOST_SIGNALS_NODE, node_ready_gpios);
static const struct gpio_dt_spec HostSignals_NodeIrqPin =
    GPIO_DT_SPEC_GET(HOST_SIGNALS_NODE, node_irq_gpios);
static const struct gpio_dt_spec HostSignals_DebugPin =
    GPIO_DT_SPEC_GET(HOST_SIGNALS_NODE, host_debug_gpios);

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
    return gpio_pin_configure_dt(&HostSignals_DebugPin, GPIO_OUTPUT_INACTIVE);
}

bool HostSignals_NodeReady(void) { return gpio_pin_get_dt(&HostSignals_NodeReadyPin) > 0; }

bool HostSignals_NodeIrqAsserted(void) { return gpio_pin_get_dt(&HostSignals_NodeIrqPin) > 0; }

int HostSignals_SetDebug(bool active) {
    return gpio_pin_set_dt(&HostSignals_DebugPin, active ? 1 : 0);
}
