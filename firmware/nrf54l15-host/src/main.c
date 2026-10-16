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

#include "HostNodeManager.h"
#include "HostSignals.h"
#include "HostTransport.h"
#include "diagnostics/DiagnosticsFault.h"
#include "node_api/NodeServices.h"
#include "virtual_io/VirtualIo.h"

LOG_MODULE_REGISTER(host_bringup, LOG_LEVEL_INF);

#define HOST_BLINK_INTERVAL_MS 500
#define HOST_SENSOR_SAMPLE_INTERVAL_MS 5000
#define HOST_ACTUATOR_DEMO_DURATION_MS 2000U
#define HOST_FAULT_CONTROL_HOLD_MS 1500
#define HOST_MAINTENANCE_CONTROL_HOLD_MS 5000
#define HOST_SHUTDOWN_CONTROL_HOLD_MS 8000

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
static atomic_t Host_ButtonChangeCount;

static void Host_ButtonPressed(const struct device *device, struct gpio_callback *callback,
                               gpio_port_pins_t pins) {
    ARG_UNUSED(device);
    ARG_UNUSED(callback);
    ARG_UNUSED(pins);

    atomic_inc(&Host_ButtonChangeCount);
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

    return gpio_pin_interrupt_configure_dt(&Host_Button, GPIO_INT_EDGE_BOTH);
}
#endif

static void Host_RequestDemoOperation(void) {
    const HostNodeManager_Status status = HostNodeManager_GetStatus();
    uint32_t operation_id;
    int result;

    if (status.node_lifecycle_state == NODE_API_LIFECYCLE_MAINTENANCE ||
        status.node_lifecycle_state == NODE_API_LIFECYCLE_SHUTDOWN) {
        LOG_WRN("demo operation suppressed while node lifecycle state is %u",
                status.node_lifecycle_state);
        return;
    }
    if (HostNodeManager_HasService(VIRTUAL_IO_ACTUATOR_SERVICE_ID)) {
        uint8_t duration[4];

        duration[0] = (uint8_t)HOST_ACTUATOR_DEMO_DURATION_MS;
        duration[1] = (uint8_t)(HOST_ACTUATOR_DEMO_DURATION_MS >> 8U);
        duration[2] = (uint8_t)(HOST_ACTUATOR_DEMO_DURATION_MS >> 16U);
        duration[3] = (uint8_t)(HOST_ACTUATOR_DEMO_DURATION_MS >> 24U);
        result = HostNodeManager_CallService(VIRTUAL_IO_ACTUATOR_SERVICE_ID,
                                             VIRTUAL_IO_ACTUATOR_COMMAND_START, duration,
                                             sizeof(duration), &operation_id);
        if (result == 0) {
            LOG_INF("requested virtual actuator operation %u", operation_id);
        }
    } else if (HostNodeManager_HasService(VIRTUAL_IO_SENSOR_SERVICE_ID)) {
        result =
            HostNodeManager_CallService(VIRTUAL_IO_SENSOR_SERVICE_ID,
                                        VIRTUAL_IO_SENSOR_COMMAND_SAMPLE, NULL, 0U, &operation_id);
        if (result == 0) {
            LOG_INF("requested virtual sensor sample operation %u", operation_id);
        }
    }
}

int main(void) {
    int result;
    bool led_on = false;
    HostLink_State reported_link_state = HOST_LINK_DISABLED;
    int64_t next_blink = 0;
    int64_t next_sensor_sample = 0;
#if HOST_HAS_BUTTON
    atomic_val_t reported_button_changes = 0;
    int64_t button_pressed_at = 0;
    bool button_down = false;
    uint8_t selected_injection = 0U;
    uint8_t injection_to_enable = 0U;
    bool fault_disable_requested = false;
    NodeApi_LifecycleState desired_lifecycle_state = NODE_API_LIFECYCLE_ONLINE;
    bool lifecycle_request_sent = false;
#endif
    bool requested_diagnostic_status = false;
    bool requested_lifecycle_status = false;
    bool requested_lifecycle_versions = false;

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

    result = HostSignals_Initialize();
    if (result != 0) {
        LOG_ERR("sideband GPIO initialization failed: %d", result);
        return result;
    }

    result = HostTransport_Initialize();
    if (result != 0) {
        LOG_WRN("node UART unavailable: %d", result);
    }
    HostNodeManager_Initialize();

    LOG_INF("nRF54L15 host Phase 5 supervision demo started (remote reset disabled)");

    for (;;) {
#if HOST_HAS_BUTTON
        atomic_val_t button_changes = atomic_get(&Host_ButtonChangeCount);

        if (button_changes != reported_button_changes) {
            const bool active = gpio_pin_get_dt(&Host_Button) > 0;

            reported_button_changes = button_changes;
            if (active && !button_down) {
                button_down = true;
                button_pressed_at = k_uptime_get();
            } else if (!active && button_down) {
                const int64_t held_ms = k_uptime_get() - button_pressed_at;

                button_down = false;
                if (held_ms >= HOST_SHUTDOWN_CONTROL_HOLD_MS) {
                    if (desired_lifecycle_state != NODE_API_LIFECYCLE_SHUTDOWN &&
                        !HostNodeManager_HasService(NODE_API_SERVICE_LIFECYCLE)) {
                        LOG_WRN("node does not advertise a compatible lifecycle service");
                    } else {
                        desired_lifecycle_state =
                            desired_lifecycle_state == NODE_API_LIFECYCLE_SHUTDOWN
                                ? NODE_API_LIFECYCLE_ONLINE
                                : NODE_API_LIFECYCLE_SHUTDOWN;
                        lifecycle_request_sent = false;
                        LOG_INF("shutdown control: %s requested",
                                desired_lifecycle_state == NODE_API_LIFECYCLE_SHUTDOWN ? "enter"
                                                                                       : "exit");
                    }
                } else if (held_ms >= HOST_MAINTENANCE_CONTROL_HOLD_MS) {
                    if (desired_lifecycle_state != NODE_API_LIFECYCLE_MAINTENANCE &&
                        !HostNodeManager_HasService(NODE_API_SERVICE_LIFECYCLE)) {
                        LOG_WRN("node does not advertise a compatible lifecycle service");
                    } else {
                        desired_lifecycle_state =
                            desired_lifecycle_state == NODE_API_LIFECYCLE_MAINTENANCE
                                ? NODE_API_LIFECYCLE_ONLINE
                                : NODE_API_LIFECYCLE_MAINTENANCE;
                        lifecycle_request_sent = false;
                        LOG_INF("maintenance control: %s requested",
                                desired_lifecycle_state == NODE_API_LIFECYCLE_MAINTENANCE ? "enter"
                                                                                          : "exit");
                    }
                } else if (held_ms >= HOST_FAULT_CONTROL_HOLD_MS) {
                    if (!HostNodeManager_HasService(NODE_API_SERVICE_DIAGNOSTICS)) {
                        LOG_WRN("node does not advertise development diagnostics");
                    } else {
                        selected_injection++;
                        if (selected_injection > (uint8_t)DIAGNOSTICS_INJECTION_MAX) {
                            selected_injection = 0U;
                        }
                        injection_to_enable = selected_injection;
                        fault_disable_requested = true;
                        LOG_INF(
                            "fault control: selected injection %u; disabling previous selection",
                            selected_injection);
                    }
                } else {
                    LOG_INF("short onboard button press (%lld ms)", held_ms);
                    Host_RequestDemoOperation();
                }
            }
        }
#endif

        HostNodeManager_Process();
        HostNodeManager_Status link_status = HostNodeManager_GetStatus();
        if (link_status.state != reported_link_state) {
            reported_link_state = link_status.state;
            LOG_INF("node link: %s (ready=%u irq=%u)", HostNodeManager_StateName(link_status.state),
                    HostSignals_NodeReady(), HostSignals_NodeIrqAsserted());
        }

        if (link_status.state != HOST_LINK_ONLINE) {
            requested_diagnostic_status = false;
            requested_lifecycle_status = false;
            requested_lifecycle_versions = false;
        } else if (HostNodeManager_HasService(NODE_API_SERVICE_LIFECYCLE) &&
                   !requested_lifecycle_status) {
            result = HostNodeManager_LifecycleGetStatus();
            if (result == 0) {
                requested_lifecycle_status = true;
            }
        } else if (HostNodeManager_HasService(NODE_API_SERVICE_LIFECYCLE) &&
                   !requested_lifecycle_versions) {
            result = HostNodeManager_LifecycleGetVersions();
            if (result == 0) {
                requested_lifecycle_versions = true;
            }
        } else if (HostNodeManager_HasService(NODE_API_SERVICE_DIAGNOSTICS) &&
                   !requested_diagnostic_status) {
            result = HostNodeManager_DiagnosticsGetStatus();
            if (result == 0) {
                requested_diagnostic_status = true;
            }
        }

#if HOST_HAS_BUTTON
        if (link_status.state != HOST_LINK_ONLINE) {
            lifecycle_request_sent = false;
        }
        if (link_status.state == HOST_LINK_ONLINE &&
            HostNodeManager_HasService(NODE_API_SERVICE_LIFECYCLE)) {
            const NodeApi_LifecycleState observed = link_status.node_lifecycle_state;

            if (observed == desired_lifecycle_state) {
                if (lifecycle_request_sent) {
                    LOG_INF("lifecycle control: node reached state %u", observed);
                }
                lifecycle_request_sent = false;
            } else if (!lifecycle_request_sent) {
                if (desired_lifecycle_state == NODE_API_LIFECYCLE_MAINTENANCE) {
                    result = HostNodeManager_LifecycleEnterMaintenance();
                } else if (desired_lifecycle_state == NODE_API_LIFECYCLE_SHUTDOWN) {
                    result = HostNodeManager_LifecycleEnterShutdown();
                } else if (observed == NODE_API_LIFECYCLE_MAINTENANCE) {
                    result = HostNodeManager_LifecycleExitMaintenance();
                } else if (observed == NODE_API_LIFECYCLE_SHUTDOWN) {
                    result = HostNodeManager_LifecycleExitShutdown();
                } else {
                    result = -EAGAIN;
                }
                if (result == 0) {
                    lifecycle_request_sent = true;
                }
            }
        }

        if (link_status.state == HOST_LINK_ONLINE && fault_disable_requested) {
            result = HostNodeManager_DiagnosticsDisableAll();
            if (result == 0) {
                fault_disable_requested = false;
            }
        } else if (link_status.state == HOST_LINK_ONLINE && injection_to_enable != 0U &&
                   link_status.node_injection_mask == 0U) {
            result = HostNodeManager_DiagnosticsEnable(injection_to_enable);
            if (result == 0) {
                LOG_INF("fault control: requested injection %u", injection_to_enable);
                injection_to_enable = 0U;
            }
        }
#endif

        if (link_status.state == HOST_LINK_ONLINE && link_status.node_diagnostic_queued != 0U) {
            (void)HostNodeManager_DiagnosticsGetRecord();
        }

        if (link_status.state == HOST_LINK_ONLINE &&
            link_status.node_lifecycle_state != NODE_API_LIFECYCLE_MAINTENANCE &&
            link_status.node_lifecycle_state != NODE_API_LIFECYCLE_SHUTDOWN &&
            HostNodeManager_HasService(VIRTUAL_IO_SENSOR_SERVICE_ID) &&
            k_uptime_get() >= next_sensor_sample) {
            uint32_t operation_id;

            next_sensor_sample = k_uptime_get() + HOST_SENSOR_SAMPLE_INTERVAL_MS;
            result = HostNodeManager_CallService(VIRTUAL_IO_SENSOR_SERVICE_ID,
                                                 VIRTUAL_IO_SENSOR_COMMAND_SAMPLE, NULL, 0U,
                                                 &operation_id);
            if (result == 0) {
                LOG_INF("requested periodic virtual sensor sample operation %u", operation_id);
            }
        }

        if (k_uptime_get() >= next_blink) {
            next_blink = k_uptime_get() + HOST_BLINK_INTERVAL_MS;
            led_on = !led_on;
            result = gpio_pin_set_dt(&Host_Led, led_on ? 1 : 0);
            if (result != 0) {
                LOG_ERR("LED update failed: %d", result);
                return result;
            }
        }

        k_msleep(10);
    }

    return 0;
}
