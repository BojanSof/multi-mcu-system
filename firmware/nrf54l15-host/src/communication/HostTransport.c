#include "HostTransport.h"
#if defined(MULTI_MCU_RELIABILITY)
#include "HostSignals.h"
#endif

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/irq.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/sys/util.h>

#define HOST_TRANSPORT_RING_CAPACITY 1024U

RING_BUF_DECLARE(HostTransport_RxRing, HOST_TRANSPORT_RING_CAPACITY);
RING_BUF_DECLARE(HostTransport_TxRing, HOST_TRANSPORT_RING_CAPACITY);

static atomic_t HostTransport_ReceivedBytes;
static atomic_t HostTransport_TransmittedBytes;
static atomic_t HostTransport_ReceiveOverflows;
static atomic_t HostTransport_TransmitOverflows;
static atomic_t HostTransport_UartErrors;

#if DT_HAS_CHOSEN(multi_mcu_node_uart)
#define HOST_TRANSPORT_UART_NODE DT_CHOSEN(multi_mcu_node_uart)
static const struct device *const HostTransport_Uart = DEVICE_DT_GET(HOST_TRANSPORT_UART_NODE);

static void HostTransport_Interrupt(const struct device *device, void *context) {
    uint8_t buffer[32];

    ARG_UNUSED(context);
    if (uart_err_check(device) > 0) {
        atomic_inc(&HostTransport_UartErrors);
    }

    while (uart_irq_update(device) != 0 && uart_irq_is_pending(device) != 0) {
        if (uart_irq_rx_ready(device) != 0) {
            int count = uart_fifo_read(device, buffer, sizeof(buffer));
            if (count > 0) {
                uint32_t accepted = ring_buf_put(&HostTransport_RxRing, buffer, (uint32_t)count);
                atomic_add(&HostTransport_ReceivedBytes, accepted);
#if defined(MULTI_MCU_RELIABILITY)
                HostSignals_Wake();
#endif
                if (accepted != (uint32_t)count) {
                    atomic_add(&HostTransport_ReceiveOverflows, (atomic_val_t)count - accepted);
                }
            }
        }

        if (uart_irq_tx_ready(device) != 0) {
            uint8_t *claim;
            uint32_t claimed = ring_buf_get_claim(&HostTransport_TxRing, &claim, sizeof(buffer));
            if (claimed == 0U) {
                uart_irq_tx_disable(device);
            } else {
                int sent = uart_fifo_fill(device, claim, (int)claimed);
                if (sent > 0) {
                    ring_buf_get_finish(&HostTransport_TxRing, (uint32_t)sent);
                    atomic_add(&HostTransport_TransmittedBytes, sent);
                } else {
                    ring_buf_get_finish(&HostTransport_TxRing, 0U);
                }
            }
        }
    }
}
#endif

int HostTransport_Initialize(void) {
#if DT_HAS_CHOSEN(multi_mcu_node_uart)
    int result;

    if (!device_is_ready(HostTransport_Uart)) {
        return -ENODEV;
    }

    result = uart_irq_callback_user_data_set(HostTransport_Uart, HostTransport_Interrupt, NULL);
    if (result != 0) {
        return result;
    }
    uart_irq_rx_enable(HostTransport_Uart);
#if defined(MULTI_MCU_PHASE12_METRICS)
    /* Count hardware-error-only interrupts promptly, not only on later RX/TX. */
    uart_irq_err_enable(HostTransport_Uart);
#endif
    return 0;
#else
    return -ENODEV;
#endif
}

bool HostTransport_IsAvailable(void) {
#if DT_HAS_CHOSEN(multi_mcu_node_uart)
    return device_is_ready(HostTransport_Uart);
#else
    return false;
#endif
}

size_t HostTransport_Read(uint8_t *data, size_t capacity) {
    if (data == NULL || capacity == 0U) {
        return 0U;
    }
    return ring_buf_get(&HostTransport_RxRing, data, (uint32_t)capacity);
}

int HostTransport_Write(const uint8_t *data, size_t length) {
#if DT_HAS_CHOSEN(multi_mcu_node_uart)
    uint32_t accepted;

    if (data == NULL || length == 0U || length > UINT32_MAX) {
        return -EINVAL;
    }
    if (ring_buf_space_get(&HostTransport_TxRing) < length) {
        atomic_add(&HostTransport_TransmitOverflows, (atomic_val_t)length);
        return -ENOSPC;
    }
    accepted = ring_buf_put(&HostTransport_TxRing, data, (uint32_t)length);
    if (accepted != length) {
        atomic_add(&HostTransport_TransmitOverflows, (atomic_val_t)(length - accepted));
        return -ENOSPC;
    }
    uart_irq_tx_enable(HostTransport_Uart);
    return 0;
#else
    ARG_UNUSED(data);
    ARG_UNUSED(length);
    return -ENODEV;
#endif
}

HostTransport_Stats HostTransport_GetStats(void) {
    HostTransport_Stats stats = {
        .received_bytes = (uint32_t)atomic_get(&HostTransport_ReceivedBytes),
        .transmitted_bytes = (uint32_t)atomic_get(&HostTransport_TransmittedBytes),
        .receive_overflows = (uint32_t)atomic_get(&HostTransport_ReceiveOverflows),
        .transmit_overflows = (uint32_t)atomic_get(&HostTransport_TransmitOverflows),
        .uart_errors = (uint32_t)atomic_get(&HostTransport_UartErrors),
    };
    return stats;
}

void HostTransport_Flush(void) {
#if DT_HAS_CHOSEN(multi_mcu_node_uart)
    unsigned int key = irq_lock();
    uart_irq_tx_disable(HostTransport_Uart);
    ring_buf_reset(&HostTransport_TxRing);
    ring_buf_reset(&HostTransport_RxRing);
    uint8_t discarded[32];
    while (uart_fifo_read(HostTransport_Uart, discarded, sizeof(discarded)) > 0) {
    }
    irq_unlock(key);
#endif
}

int HostTransport_ConfigureRom(bool enabled) {
#if DT_HAS_CHOSEN(multi_mcu_node_uart) && defined(CONFIG_UART_USE_RUNTIME_CONFIGURE)
    struct uart_config config;
    int result = uart_config_get(HostTransport_Uart, &config);
    if (result != 0) {
        return result;
    }
    HostTransport_Flush();
    uart_irq_rx_disable(HostTransport_Uart);
    config.baudrate = 115200U;
    config.parity = enabled ? UART_CFG_PARITY_EVEN : UART_CFG_PARITY_NONE;
    config.stop_bits = UART_CFG_STOP_BITS_1;
    config.data_bits = UART_CFG_DATA_BITS_8;
    config.flow_ctrl = UART_CFG_FLOW_CTRL_NONE;
    result = uart_configure(HostTransport_Uart, &config);
    uart_irq_rx_enable(HostTransport_Uart);
    return result;
#else
    ARG_UNUSED(enabled);
    return -ENOTSUP;
#endif
}
