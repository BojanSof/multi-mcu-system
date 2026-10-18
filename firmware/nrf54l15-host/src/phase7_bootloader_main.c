#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "HostBootloaderClient.h"
#include "HostTransport.h"

LOG_MODULE_REGISTER(host_bootloader_demo, LOG_LEVEL_INF);

#define HOST_LED_NODE DT_ALIAS(led0)
#define HOST_BLINK_INTERVAL_MS 500

static const struct gpio_dt_spec Host_Led = GPIO_DT_SPEC_GET(HOST_LED_NODE, gpios);

#if defined(MULTI_MCU_PHASE7_STM_IMAGE_EMBEDDED)
static const uint8_t Host_StmImage[] = {
#include <phase7_stm_image.inc>
};
#endif

int main(void) {
    HostBootloaderClient_State reported_state = HOST_BOOTLOADER_CLIENT_IDLE;
    int64_t next_blink = 0;
    bool led_on = false;
    int result;

    if (!gpio_is_ready_dt(&Host_Led)) {
        return -ENODEV;
    }
    result = gpio_pin_configure_dt(&Host_Led, GPIO_OUTPUT_INACTIVE);
    if (result != 0) {
        return result;
    }
    result = HostTransport_Initialize();
    if (result != 0) {
        LOG_ERR("STM bootloader UART unavailable: %d", result);
        return result;
    }
    HostBootloaderClient_Initialize();
    result = HostBootloaderClient_Query();
    if (result != 0) {
        LOG_ERR("initial boot state query failed: %d", result);
        return result;
    }
    LOG_INF("Phase 7 STM bootloader recovery client started");

    for (;;) {
        HostBootloaderClient_Status status;

        HostBootloaderClient_Process();
        status = HostBootloaderClient_GetStatus();
        if (status.state != reported_state) {
            reported_state = status.state;
            LOG_INF("STM update: %s (%u/%u bytes, protocol=%u, error=%d)",
                    HostBootloaderClient_StateName(status.state), status.bytes_written,
                    status.image_size, status.last_bootloader_status, status.last_error);
#if defined(MULTI_MCU_PHASE7_STM_IMAGE_EMBEDDED)
            if (status.state == HOST_BOOTLOADER_CLIENT_READY) {
                result = HostBootloaderClient_StartUpdate(Host_StmImage, sizeof(Host_StmImage));
                if (result != 0) {
                    LOG_ERR("cannot start STM update: %d", result);
                }
            } else if (status.state == HOST_BOOTLOADER_CLIENT_COMPLETE) {
                LOG_INF("signed image accepted and pending; reset STM32 to start its trial boot");
            }
#else
            if (status.state == HOST_BOOTLOADER_CLIENT_READY) {
                LOG_INF("recovery link ready; no image embedded in this build");
            }
#endif
        }
        if (k_uptime_get() >= next_blink) {
            next_blink = k_uptime_get() + HOST_BLINK_INTERVAL_MS;
            led_on = !led_on;
            (void)gpio_pin_set_dt(&Host_Led, led_on ? 1 : 0);
        }
        k_msleep(10);
    }
    return 0;
}
