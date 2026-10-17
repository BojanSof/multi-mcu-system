cmake_minimum_required(VERSION 3.20.0)

# Zephyr discovers out-of-tree modules while find_package() runs, so this list
# must be complete before loading Zephyr.
get_filename_component(MULTI_MCU_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
list(APPEND EXTRA_ZEPHYR_MODULES
    "${MULTI_MCU_ROOT}/libraries/protocol"
    "${MULTI_MCU_ROOT}/libraries/node-api"
    "${MULTI_MCU_ROOT}/libraries/diagnostics"
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(multi_mcu_host C)

option(MULTI_MCU_PHASE6_UNCONFIRMED_TRIAL_DEMO
       "Build a development image that deliberately remains unconfirmed" OFF)

target_sources(app PRIVATE
    src/main.c
    src/communication/HostTransport.c
    src/node_manager/HostNodeManager.c
    src/platform/HostSignals.c
)

target_include_directories(app PRIVATE
    include
    "${MULTI_MCU_ROOT}/examples/virtual-io/include"
)
if(MULTI_MCU_PHASE6_UNCONFIRMED_TRIAL_DEMO)
    target_compile_definitions(app PRIVATE MULTI_MCU_PHASE6_UNCONFIRMED_TRIAL_DEMO=1)
endif()
