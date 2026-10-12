add_library(stm32u083_platform INTERFACE)
add_library(Stm32U083::Platform ALIAS stm32u083_platform)

target_sources(stm32u083_platform INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/src/PlatformBoard.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/PlatformLog.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/PlatformSideband.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/PlatformTransport.c")
target_include_directories(stm32u083_platform INTERFACE include)
target_compile_features(stm32u083_platform INTERFACE c_std_11)
target_compile_definitions(stm32u083_platform INTERFACE
    MULTI_MCU_BOARD_NUCLEO_U083RC=1
    MULTI_MCU_UART1_PINS_VERIFIED=1
    MULTI_MCU_SIDEBAND_PINS_VERIFIED=1)
target_link_libraries(stm32u083_platform INTERFACE
    CMSIS::STM32::U083RC
    HAL::STM32::U0::CORTEX
    HAL::STM32::U0::FLASH
    HAL::STM32::U0::GPIO
    HAL::STM32::U0::PWR
    HAL::STM32::U0::PWREx
    HAL::STM32::U0::RCC
    HAL::STM32::U0::RCCEx
    HAL::STM32::U0::UART
    HAL::STM32::U0::UARTEx)
