cmake_minimum_required(VERSION 3.24)
project(MultiMcuLab VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(MULTI_MCU_WARNINGS_AS_ERRORS "Treat project warnings as errors" ON)
include(CTest)

function(multi_mcu_apply_warnings target)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
        if(MULTI_MCU_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

add_subdirectory(libraries/protocol)
add_subdirectory(libraries/node-api)
add_subdirectory(examples/virtual-io)
add_subdirectory(libraries/diagnostics)
add_subdirectory(libraries/firmware-image)
add_subdirectory(libraries/boot-protocol)

if(BUILD_TESTING)
    add_subdirectory(firmware/nrf54l15-host/tests)
endif()
