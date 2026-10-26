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
    "${MULTI_MCU_ROOT}/libraries/stm32-rom-boot"
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(multi_mcu_host C)

option(MULTI_MCU_PHASE6_UNCONFIRMED_TRIAL_DEMO
       "Build a development image that deliberately remains unconfirmed" OFF)
option(MULTI_MCU_PHASE7_STM_BOOTLOADER_DEMO
       "Build the opt-in STM32 custom bootloader recovery/update client" OFF)
option(MULTI_MCU_PHASE9_CONTROL "Build the opt-in MCUmanager host control group" OFF)
option(MULTI_MCU_PHASE10_UPDATE "Build PC-streamed STM32 updates and supervision" OFF)
option(MULTI_MCU_PHASE11_ROM "Build factory ROM bootloader repair" OFF)
option(MULTI_MCU_PHASE12_METRICS "Build operator reliability metrics" OFF)
option(MULTI_MCU_RELIABILITY "Build watchdog and automatic node recovery" OFF)

# Sysbuild stores image-scoped command-line values as properties; plain option()
# does not import them into the child application's CMake cache.
if(SYSBUILD)
    foreach(profile_option MULTI_MCU_PHASE6_UNCONFIRMED_TRIAL_DEMO
                           MULTI_MCU_PHASE7_STM_BOOTLOADER_DEMO
                           MULTI_MCU_PHASE9_CONTROL MULTI_MCU_PHASE10_UPDATE
                           MULTI_MCU_PHASE11_ROM MULTI_MCU_PHASE12_METRICS MULTI_MCU_RELIABILITY)
        unset(profile_value)
        zephyr_get(profile_value VAR ${profile_option})
        if(DEFINED profile_value)
            set(${profile_option} "${profile_value}" CACHE BOOL "Selected firmware profile" FORCE)
        endif()
    endforeach()
endif()
if(MULTI_MCU_PHASE10_UPDATE AND NOT MULTI_MCU_PHASE9_CONTROL)
    message(FATAL_ERROR "Phase 10 requires Phase 9 control")
endif()
if(MULTI_MCU_PHASE11_ROM AND NOT MULTI_MCU_PHASE10_UPDATE)
    message(FATAL_ERROR "Phase 11 requires Phase 10")
endif()
if(MULTI_MCU_PHASE12_METRICS AND NOT MULTI_MCU_PHASE11_ROM)
    message(FATAL_ERROR "Phase 12 requires Phase 11 recovery")
endif()
if(MULTI_MCU_PHASE9_CONTROL AND MULTI_MCU_PHASE7_STM_BOOTLOADER_DEMO)
    message(FATAL_ERROR "Phase 9 control and Phase 7 demo are independent profiles")
endif()
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

if(MULTI_MCU_PHASE9_CONTROL)
    if(NOT CONFIG_MCUMGR)
        message(FATAL_ERROR "Phase 9 requires phase6_mcumgr.conf and phase9_control.conf")
    endif()
    target_sources(app PRIVATE src/control/HostControl.c)
    target_compile_definitions(app PRIVATE MULTI_MCU_PHASE9_CONTROL=1)
endif()
if(MULTI_MCU_PHASE10_UPDATE)
    target_sources(app PRIVATE src/control/HostUpdateAdapter.c src/bootloader/HostStmUpdate.c)
    target_compile_definitions(app PRIVATE MULTI_MCU_PHASE10_UPDATE=1)
    target_compile_options(app PRIVATE -fstack-usage)
endif()
if(MULTI_MCU_PHASE11_ROM)
    target_compile_definitions(app PRIVATE MULTI_MCU_PHASE11_ROM=1)
endif()
if(MULTI_MCU_PHASE12_METRICS)
    target_compile_definitions(app PRIVATE MULTI_MCU_PHASE12_METRICS=1)
endif()

target_include_directories(app PRIVATE
    include
    "${MULTI_MCU_ROOT}/examples/virtual-io/include"
)
if(MULTI_MCU_PHASE6_UNCONFIRMED_TRIAL_DEMO)
    target_compile_definitions(app PRIVATE MULTI_MCU_PHASE6_UNCONFIRMED_TRIAL_DEMO=1)
endif()

if(MULTI_MCU_RELIABILITY)
    if(NOT MULTI_MCU_PHASE12_METRICS OR NOT CONFIG_WATCHDOG OR NOT CONFIG_HWINFO)
        message(FATAL_ERROR "Reliability requires Phase12 plus reliability.conf")
    endif()
    target_sources(app PRIVATE src/platform/HostReliability.c src/platform/HostRecoveryPolicy.c)
    target_compile_definitions(app PRIVATE MULTI_MCU_RELIABILITY=1)
endif()
