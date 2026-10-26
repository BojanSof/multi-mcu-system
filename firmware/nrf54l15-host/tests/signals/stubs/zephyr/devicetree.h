#ifndef SIGNALS_TEST_DEVICETREE_H
#define SIGNALS_TEST_DEVICETREE_H
#define DT_PATH(name) name
#define GPIO_DT_SPEC_GET(node, property) GPIO_TEST_##property
#define GPIO_TEST_node_ready_gpios {&Test_Port, 6U, 0U}
#define GPIO_TEST_node_irq_gpios {&Test_Port, 8U, 0U}
#define GPIO_TEST_host_debug_gpios {&Test_Port, 10U, 0U}
#endif
