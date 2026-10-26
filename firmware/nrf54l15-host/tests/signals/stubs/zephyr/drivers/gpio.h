#ifndef SIGNALS_TEST_GPIO_H
#define SIGNALS_TEST_GPIO_H
#include <stdbool.h>
#include <stdint.h>
#define BIT(pin) (UINT32_C(1) << (pin))
#define GPIO_INPUT 1U
#define GPIO_OUTPUT_INACTIVE 2U
#define GPIO_INT_EDGE_BOTH 3U
#define GPIO_INT_EDGE_TO_ACTIVE 4U
struct device {
    int unused;
};
extern struct device Test_Port;
typedef uint32_t gpio_flags_t;
typedef uint32_t gpio_port_pins_t;
struct gpio_dt_spec {
    const struct device *port;
    unsigned pin;
    gpio_flags_t flags;
};
struct gpio_callback {
    void (*handler)(const struct device *, struct gpio_callback *, gpio_port_pins_t);
    gpio_port_pins_t mask;
};
bool gpio_is_ready_dt(const struct gpio_dt_spec *pin);
int gpio_pin_configure_dt(const struct gpio_dt_spec *pin, gpio_flags_t flags);
void gpio_init_callback(struct gpio_callback *callback,
                        void (*handler)(const struct device *, struct gpio_callback *,
                                        gpio_port_pins_t),
                        gpio_port_pins_t mask);
int gpio_add_callback(const struct device *port, struct gpio_callback *callback);
int gpio_remove_callback(const struct device *port, struct gpio_callback *callback);
int gpio_pin_interrupt_configure_dt(const struct gpio_dt_spec *pin, gpio_flags_t flags);
int gpio_pin_get_dt(const struct gpio_dt_spec *pin);
int gpio_pin_set_dt(const struct gpio_dt_spec *pin, int value);
#endif
