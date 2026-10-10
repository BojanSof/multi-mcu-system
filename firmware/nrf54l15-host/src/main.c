#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(host_bringup, LOG_LEVEL_INF);

#define HOST_BLINK_INTERVAL_MS 500

#define HOST_LED_NODE DT_ALIAS(led0)
#define HOST_BUTTON_NODE DT_ALIAS(sw0)

#if !DT_NODE_HAS_STATUS(HOST_LED_NODE, okay)
#error "The selected board must provide an enabled led0 devicetree alias"
#endif

static const struct gpio_dt_spec Host_Led = GPIO_DT_SPEC_GET(HOST_LED_NODE, gpios);

#if DT_NODE_HAS_STATUS(HOST_BUTTON_NODE, okay)
#define HOST_HAS_BUTTON 1
static const struct gpio_dt_spec Host_Button = GPIO_DT_SPEC_GET(HOST_BUTTON_NODE, gpios);
static struct gpio_callback Host_ButtonCallback;
static atomic_t Host_ButtonPressCount;

static void Host_ButtonPressed(const struct device *device, struct gpio_callback *callback,
                               gpio_port_pins_t pins) {
    ARG_UNUSED(device);
    ARG_UNUSED(callback);
    ARG_UNUSED(pins);

    atomic_inc(&Host_ButtonPressCount);
}
#else
#define HOST_HAS_BUTTON 0
#endif

static int Host_ConfigureLed(void) {
    if (!gpio_is_ready_dt(&Host_Led)) {
        LOG_ERR("led0 GPIO controller is not ready");
        return -ENODEV;
    }

    return gpio_pin_configure_dt(&Host_Led, GPIO_OUTPUT_INACTIVE);
}

#if HOST_HAS_BUTTON
static int Host_ConfigureButton(void) {
    int result;

    if (!gpio_is_ready_dt(&Host_Button)) {
        LOG_ERR("sw0 GPIO controller is not ready");
        return -ENODEV;
    }

    result = gpio_pin_configure_dt(&Host_Button, GPIO_INPUT);
    if (result != 0) {
        return result;
    }

    gpio_init_callback(&Host_ButtonCallback, Host_ButtonPressed, BIT(Host_Button.pin));
    result = gpio_add_callback(Host_Button.port, &Host_ButtonCallback);
    if (result != 0) {
        return result;
    }

    return gpio_pin_interrupt_configure_dt(&Host_Button, GPIO_INT_EDGE_TO_ACTIVE);
}
#endif

int main(void) {
    int result;
    bool led_on = false;
#if HOST_HAS_BUTTON
    atomic_val_t reported_button_presses = 0;
#endif

    result = Host_ConfigureLed();
    if (result != 0) {
        LOG_ERR("LED initialization failed: %d", result);
        return result;
    }

#if HOST_HAS_BUTTON
    result = Host_ConfigureButton();
    if (result != 0) {
        LOG_ERR("button initialization failed: %d", result);
        return result;
    }
#else
    LOG_WRN("selected board has no enabled sw0 alias; button test disabled");
#endif

    LOG_INF("nRF54L15 host Phase 1 bring-up started");

    for (;;) {
#if HOST_HAS_BUTTON
        atomic_val_t button_presses = atomic_get(&Host_ButtonPressCount);

        if (button_presses != reported_button_presses) {
            reported_button_presses = button_presses;
            LOG_INF("onboard button presses: %ld", (long)button_presses);
        }
#endif

        led_on = !led_on;
        result = gpio_pin_set_dt(&Host_Led, led_on ? 1 : 0);
        if (result != 0) {
            LOG_ERR("LED update failed: %d", result);
            return result;
        }

        k_msleep(HOST_BLINK_INTERVAL_MS);
    }

    return 0;
}
