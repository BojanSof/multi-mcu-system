cmake_minimum_required(VERSION 3.24)

if(NOT CMAKE_TOOLCHAIN_FILE)
    message(FATAL_ERROR
        "Configure with third_party/stm32-cmake/cmake/stm32_gcc.cmake as CMAKE_TOOLCHAIN_FILE")
endif()

project(multi_mcu_stm32_application VERSION 0.1.0 LANGUAGES C ASM)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/Dependencies.cmake")
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../platform"
                 "${CMAKE_CURRENT_BINARY_DIR}/platform")

add_executable(multi_mcu_stm32_application
    src/main.c
    src/AppRtos.c
    src/Stm32Interrupts.c)
target_include_directories(multi_mcu_stm32_application PRIVATE
    include
    config)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/Stm32Targets.cmake")
multi_mcu_configure_stm32_application(multi_mcu_stm32_application)
stm32_print_size_of_target(multi_mcu_stm32_application)
