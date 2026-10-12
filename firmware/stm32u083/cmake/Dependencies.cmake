include_guard(GLOBAL)

set(MULTI_MCU_STM32_CMAKE_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../../../third_party/stm32-cmake"
    CACHE PATH "Path to the pinned ObKo/stm32-cmake checkout")

if(NOT EXISTS "${MULTI_MCU_STM32_CMAKE_DIR}/cmake/stm32_gcc.cmake")
    message(FATAL_ERROR
        "Pinned stm32-cmake checkout not found at ${MULTI_MCU_STM32_CMAKE_DIR}. "
        "Initialize the third_party/stm32-cmake submodule first.")
endif()

list(PREPEND CMAKE_MODULE_PATH "${MULTI_MCU_STM32_CMAKE_DIR}/cmake")

# These helpers obtain the separately pinned ST CMSIS-device and HAL repositories.
# FetchContent's cache is reusable; an integrator may instead set the corresponding
# FETCHCONTENT_SOURCE_DIR_* variables for an offline checkout.
stm32_fetch_cmsis(U0)
stm32_fetch_hal(U0)

find_package(CMSIS COMPONENTS STM32U083RC REQUIRED)
find_package(HAL COMPONENTS STM32U0 REQUIRED)

set(FREERTOS_PATH "" CACHE PATH "Path to the pinned FreeRTOS-Kernel checkout")
if(NOT FREERTOS_PATH)
    message(FATAL_ERROR
        "FREERTOS_PATH must name the pinned FreeRTOS-Kernel checkout. "
        "The STM32 application never downloads an unpinned kernel implicitly.")
endif()
find_package(FreeRTOS COMPONENTS ARM_CM0 REQUIRED)

# FreeRTOS 11 split the Cortex-M0 assembly routines into portasm.c. The
# currently pinned stm32-cmake finder predates that split.
if(EXISTS "${FREERTOS_PATH}/portable/GCC/ARM_CM0/portasm.c")
    target_sources(FreeRTOS::ARM_CM0 INTERFACE
        "${FREERTOS_PATH}/portable/GCC/ARM_CM0/portasm.c")
endif()
