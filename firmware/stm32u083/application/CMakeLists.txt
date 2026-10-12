cmake_minimum_required(VERSION 3.24)

if(NOT CMAKE_TOOLCHAIN_FILE)
    message(FATAL_ERROR
        "Configure with third_party/stm32-cmake/cmake/stm32_gcc.cmake as CMAKE_TOOLCHAIN_FILE")
endif()

project(multi_mcu_stm32_application VERSION 0.1.0 LANGUAGES C ASM)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/Dependencies.cmake")
get_filename_component(MULTI_MCU_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
add_subdirectory("${MULTI_MCU_ROOT}/libraries/protocol"
                 "${CMAKE_CURRENT_BINARY_DIR}/protocol")
add_subdirectory("${MULTI_MCU_ROOT}/libraries/node-api"
                 "${CMAKE_CURRENT_BINARY_DIR}/node-api")
# Portable targets do not inherit MCU flags; apply them only in this cross build.
target_compile_options(protocol PRIVATE -mthumb -mcpu=cortex-m0plus)
target_compile_options(node_api PRIVATE -mthumb -mcpu=cortex-m0plus)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../platform"
                 "${CMAKE_CURRENT_BINARY_DIR}/platform")

add_executable(multi_mcu_stm32_application
    src/main.c
    src/AppRtos.c
    src/NodeTransport.c
    src/Stm32Interrupts.c)
target_include_directories(multi_mcu_stm32_application PRIVATE
    include
    config)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/Stm32Targets.cmake")
multi_mcu_configure_stm32_application(multi_mcu_stm32_application)
target_link_libraries(multi_mcu_stm32_application PRIVATE
    Protocol::Protocol
    NodeApi::NodeApi)
stm32_print_size_of_target(multi_mcu_stm32_application)
