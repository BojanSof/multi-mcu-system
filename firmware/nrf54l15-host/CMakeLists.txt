cmake_minimum_required(VERSION 3.20.0)

# Zephyr discovers out-of-tree modules while find_package() runs, so this list
# must be complete before loading Zephyr.
get_filename_component(MULTI_MCU_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
list(APPEND EXTRA_ZEPHYR_MODULES
    "${MULTI_MCU_ROOT}/libraries/protocol"
    "${MULTI_MCU_ROOT}/libraries/node-api"
    "${MULTI_MCU_ROOT}/libraries/diagnostics"
    "${MULTI_MCU_ROOT}/libraries/boot-protocol"
    "${MULTI_MCU_ROOT}/libraries/firmware-image"
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(multi_mcu_host C)

option(MULTI_MCU_PHASE6_UNCONFIRMED_TRIAL_DEMO
       "Build a development image that deliberately remains unconfirmed" OFF)
option(MULTI_MCU_PHASE7_STM_BOOTLOADER_DEMO
       "Build the opt-in STM32 custom bootloader recovery/update client" OFF)
set(MULTI_MCU_PHASE7_STM_IMAGE "" CACHE FILEPATH
    "Optional signed STM32 slot image embedded in the Phase 7 demo")

if(MULTI_MCU_PHASE7_STM_BOOTLOADER_DEMO)
    target_sources(app PRIVATE
        src/phase7_bootloader_main.c
        src/bootloader/HostBootloaderClient.c
        src/communication/HostTransport.c
    )
    target_compile_definitions(app PRIVATE MULTI_MCU_PHASE7_STM_BOOTLOADER_DEMO=1)
    if(MULTI_MCU_PHASE7_STM_IMAGE)
        get_filename_component(MULTI_MCU_PHASE7_STM_IMAGE_ABS
            "${MULTI_MCU_PHASE7_STM_IMAGE}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        if(NOT EXISTS "${MULTI_MCU_PHASE7_STM_IMAGE_ABS}")
            message(FATAL_ERROR "STM32 signed image not found: ${MULTI_MCU_PHASE7_STM_IMAGE_ABS}")
        endif()
        generate_inc_file_for_target(app "${MULTI_MCU_PHASE7_STM_IMAGE_ABS}"
                                     "${ZEPHYR_BINARY_DIR}/include/generated/phase7_stm_image.inc")
        target_compile_definitions(app PRIVATE MULTI_MCU_PHASE7_STM_IMAGE_EMBEDDED=1)
    endif()
else()
    target_sources(app PRIVATE
        src/main.c
        src/communication/HostTransport.c
        src/node_manager/HostNodeManager.c
        src/platform/HostSignals.c
    )
endif()

target_include_directories(app PRIVATE
    include
    "${MULTI_MCU_ROOT}/examples/virtual-io/include"
)
if(MULTI_MCU_PHASE6_UNCONFIRMED_TRIAL_DEMO)
    target_compile_definitions(app PRIVATE MULTI_MCU_PHASE6_UNCONFIRMED_TRIAL_DEMO=1)
endif()
