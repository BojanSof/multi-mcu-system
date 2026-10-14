cmake_minimum_required(VERSION 3.24)

if(NOT CMAKE_TOOLCHAIN_FILE)
    message(FATAL_ERROR
        "Configure with third_party/stm32-cmake/cmake/stm32_gcc.cmake as CMAKE_TOOLCHAIN_FILE")
endif()

project(multi_mcu_stm32_application VERSION 0.1.0 LANGUAGES C ASM)

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
option(MULTI_MCU_EARLY_READY_BOOT_DEMO
       "Intentionally assert READY before UART init (development analyzer lesson)" OFF)
if(MULTI_MCU_ENABLE_DEV_FAULTS AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(FATAL_ERROR "Development fault injection is permitted only in Debug builds")
endif()
if(MULTI_MCU_EARLY_READY_BOOT_DEMO AND NOT MULTI_MCU_ENABLE_DEV_FAULTS)
    message(FATAL_ERROR "EARLY_READY boot demo requires development fault injection")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/Dependencies.cmake")
get_filename_component(MULTI_MCU_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
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
if(MULTI_MCU_EARLY_READY_BOOT_DEMO)
    target_compile_definitions(multi_mcu_stm32_application PRIVATE
        MULTI_MCU_EARLY_READY_BOOT_DEMO=1)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/Stm32Targets.cmake")
multi_mcu_configure_stm32_application(multi_mcu_stm32_application)
target_link_libraries(multi_mcu_stm32_application PRIVATE
    Protocol::Protocol
    NodeApi::NodeApi
    Diagnostics::Diagnostics
    VirtualIo::VirtualIo)
stm32_print_size_of_target(multi_mcu_stm32_application)
