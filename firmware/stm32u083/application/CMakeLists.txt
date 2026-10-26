cmake_minimum_required(VERSION 3.24)

if(NOT CMAKE_TOOLCHAIN_FILE)
    message(FATAL_ERROR
        "Configure with third_party/stm32-cmake/cmake/stm32_gcc.cmake as CMAKE_TOOLCHAIN_FILE")
endif()

project(multi_mcu_stm32_application VERSION 0.6.0 LANGUAGES C ASM)

set(MULTI_MCU_STM32_LAYOUT_PROFILE "rom_bringup" CACHE STRING
    "STM32 link profile (rom_bringup or custom_ab)")
set_property(CACHE MULTI_MCU_STM32_LAYOUT_PROFILE PROPERTY STRINGS rom_bringup custom_ab)
option(MULTI_MCU_RELIABILITY "Enable persistent node faults and reliable event delivery" OFF)
if(MULTI_MCU_RELIABILITY AND NOT MULTI_MCU_STM32_LAYOUT_PROFILE STREQUAL "custom_ab")
    message(FATAL_ERROR "MULTI_MCU_RELIABILITY requires custom_ab to reserve fault journal flash")
endif()
set(MULTI_MCU_STM32_SLOT "" CACHE STRING "Custom A/B slot number (0 or 1)")
if(NOT MULTI_MCU_STM32_LAYOUT_PROFILE MATCHES "^(rom_bringup|custom_ab)$")
    message(FATAL_ERROR "Unknown MULTI_MCU_STM32_LAYOUT_PROFILE")
endif()

set(MULTI_MCU_STM32_PERSONALITY "A" CACHE STRING
    "STM32 virtual-I/O personality (A: GPIO+sensor, B: GPIO+sensor+actuator)")
set_property(CACHE MULTI_MCU_STM32_PERSONALITY PROPERTY STRINGS A B)
if(NOT MULTI_MCU_STM32_PERSONALITY MATCHES "^(A|B)$")
    message(FATAL_ERROR "MULTI_MCU_STM32_PERSONALITY must be A or B")
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(MULTI_MCU_DEV_FAULTS_DEFAULT ON)
else()
    set(MULTI_MCU_DEV_FAULTS_DEFAULT OFF)
endif()
option(MULTI_MCU_ENABLE_DEV_FAULTS "Enable development-only runtime fault injection"
       ${MULTI_MCU_DEV_FAULTS_DEFAULT})
option(MULTI_MCU_TRIAL_FAILURE_DEMO
       "Development-only signed trial that resets instead of confirming" OFF)
if(MULTI_MCU_TRIAL_FAILURE_DEMO AND
   (NOT MULTI_MCU_ENABLE_DEV_FAULTS OR NOT MULTI_MCU_STM32_LAYOUT_PROFILE STREQUAL "custom_ab"))
    message(FATAL_ERROR "Trial failure demo requires Debug faults and custom_ab layout")
endif()
option(MULTI_MCU_EARLY_READY_BOOT_DEMO
       "Intentionally assert READY before UART init (development analyzer lesson)" OFF)
option(MULTI_MCU_INCOMPATIBLE_SENSOR_DEMO
       "Advertise an incompatible virtual-sensor major for compatibility testing" OFF)
if(MULTI_MCU_ENABLE_DEV_FAULTS AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(FATAL_ERROR "Development fault injection is permitted only in Debug builds")
endif()
if(MULTI_MCU_EARLY_READY_BOOT_DEMO AND NOT MULTI_MCU_ENABLE_DEV_FAULTS)
    message(FATAL_ERROR "EARLY_READY boot demo requires development fault injection")
endif()
if(MULTI_MCU_INCOMPATIBLE_SENSOR_DEMO AND NOT MULTI_MCU_ENABLE_DEV_FAULTS)
    message(FATAL_ERROR "Incompatible-service demo requires development fault injection")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/Dependencies.cmake")
get_filename_component(MULTI_MCU_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
if(MULTI_MCU_STM32_LAYOUT_PROFILE STREQUAL "custom_ab")
    if(NOT MULTI_MCU_STM32_SLOT MATCHES "^[01]$")
        message(FATAL_ERROR "custom_ab requires MULTI_MCU_STM32_SLOT=0 or 1")
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(MULTI_MCU_LAYOUT_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated-layout")
    execute_process(
        COMMAND "${Python3_EXECUTABLE}"
                "${MULTI_MCU_ROOT}/scripts/generate_flash_layout.py"
                "${CMAKE_CURRENT_LIST_DIR}/../memory/flash_layout.yaml"
                "${MULTI_MCU_LAYOUT_OUTPUT}"
        COMMAND_ERROR_IS_FATAL ANY)
    if(MULTI_MCU_STM32_SLOT STREQUAL "0")
        set(MULTI_MCU_SLOT_LINKER "${MULTI_MCU_LAYOUT_OUTPUT}/slot0.ld")
        set(MULTI_MCU_SLOT_VECTOR_ADDRESS 0x08010200)
    else()
        set(MULTI_MCU_SLOT_LINKER "${MULTI_MCU_LAYOUT_OUTPUT}/slot1.ld")
        set(MULTI_MCU_SLOT_VECTOR_ADDRESS 0x08026200)
    endif()
    # The CMSIS target contributes a whole-flash default script. A custom slot
    # build must use only the generated layout-authoritative script.
    set_property(TARGET CMSIS::STM32::U083RC PROPERTY INTERFACE_LINK_OPTIONS "")
endif()
add_subdirectory("${MULTI_MCU_ROOT}/libraries/protocol"
                 "${CMAKE_CURRENT_BINARY_DIR}/protocol")
add_subdirectory("${MULTI_MCU_ROOT}/libraries/node-api"
                 "${CMAKE_CURRENT_BINARY_DIR}/node-api")
add_subdirectory("${MULTI_MCU_ROOT}/examples/virtual-io"
                 "${CMAKE_CURRENT_BINARY_DIR}/virtual-io")
add_subdirectory("${MULTI_MCU_ROOT}/libraries/diagnostics"
                 "${CMAKE_CURRENT_BINARY_DIR}/diagnostics")
# Portable targets do not inherit MCU flags; apply them only in this cross build.
target_compile_options(protocol PRIVATE -mthumb -mcpu=cortex-m0plus)
target_compile_options(node_api PRIVATE -mthumb -mcpu=cortex-m0plus)
target_compile_options(virtual_io PRIVATE -mthumb -mcpu=cortex-m0plus)
target_compile_options(diagnostics PRIVATE -mthumb -mcpu=cortex-m0plus)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../platform"
                 "${CMAKE_CURRENT_BINARY_DIR}/platform")

add_executable(multi_mcu_stm32_application
    src/main.c
    src/AppRtos.c
    src/AppDiagnostics.c
    src/AppFaultHistory.c
    src/AppLifecycle.c
    src/AppServices.c
    src/NodeTransport.c
    src/Stm32Interrupts.c)
target_include_directories(multi_mcu_stm32_application PRIVATE
    include
    config)
target_compile_definitions(multi_mcu_stm32_application PRIVATE
    MULTI_MCU_PERSONALITY_${MULTI_MCU_STM32_PERSONALITY}=1
    MULTI_MCU_FIRMWARE_VERSION="${PROJECT_VERSION}")
if(MULTI_MCU_ENABLE_DEV_FAULTS)
    target_compile_definitions(multi_mcu_stm32_application PRIVATE MULTI_MCU_DEV_FAULTS=1)
endif()
if(MULTI_MCU_RELIABILITY)
    target_compile_definitions(multi_mcu_stm32_application PRIVATE MULTI_MCU_RELIABILITY=1)
    # stm32-cmake's FLASH target omits U0's extended erase implementation.
    target_sources(multi_mcu_stm32_application PRIVATE
        "${STM32_HAL_U0_PATH}/Src/stm32u0xx_hal_flash_ex.c")
endif()
if(MULTI_MCU_TRIAL_FAILURE_DEMO)
    target_compile_definitions(multi_mcu_stm32_application PRIVATE MULTI_MCU_TRIAL_FAILURE_DEMO=1)
endif()
if(MULTI_MCU_EARLY_READY_BOOT_DEMO)
    target_compile_definitions(multi_mcu_stm32_application PRIVATE
        MULTI_MCU_EARLY_READY_BOOT_DEMO=1)
endif()
if(MULTI_MCU_INCOMPATIBLE_SENSOR_DEMO)
    target_compile_definitions(multi_mcu_stm32_application PRIVATE
        MULTI_MCU_INCOMPATIBLE_SENSOR_DEMO=1)
endif()
if(MULTI_MCU_STM32_LAYOUT_PROFILE STREQUAL "custom_ab")
    target_sources(multi_mcu_stm32_application PRIVATE src/AppBootControl.c)
    target_include_directories(multi_mcu_stm32_application PRIVATE "${MULTI_MCU_LAYOUT_OUTPUT}")
    target_include_directories(multi_mcu_stm32_application PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/../bootloader/include")
    target_compile_definitions(multi_mcu_stm32_application PRIVATE
        MULTI_MCU_CUSTOM_BOOT_SLOT=${MULTI_MCU_STM32_SLOT}
        MULTI_MCU_SLOT_VECTOR_ADDRESS=${MULTI_MCU_SLOT_VECTOR_ADDRESS})
    target_link_options(multi_mcu_stm32_application PRIVATE "-T${MULTI_MCU_SLOT_LINKER}")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/Stm32Targets.cmake")
multi_mcu_configure_stm32_application(multi_mcu_stm32_application)
target_link_libraries(multi_mcu_stm32_application PRIVATE
    Protocol::Protocol
    NodeApi::NodeApi
    Diagnostics::Diagnostics
    VirtualIo::VirtualIo)
stm32_print_size_of_target(multi_mcu_stm32_application)
add_custom_command(TARGET multi_mcu_stm32_application POST_BUILD
    COMMAND "${CMAKE_OBJCOPY}" -O binary
            "$<TARGET_FILE:multi_mcu_stm32_application>"
            "${CMAKE_CURRENT_BINARY_DIR}/multi_mcu_stm32_application.bin"
    VERBATIM)
