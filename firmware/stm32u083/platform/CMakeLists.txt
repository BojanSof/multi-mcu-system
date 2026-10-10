add_library(stm32u083_platform STATIC
    src/PlatformBoard.c
    src/PlatformLog.c)
add_library(Stm32U083::Platform ALIAS stm32u083_platform)

target_include_directories(stm32u083_platform PUBLIC include)
target_compile_features(stm32u083_platform PUBLIC c_std_11)
target_compile_definitions(stm32u083_platform PUBLIC
    MULTI_MCU_BOARD_NUCLEO_U083RC=1)
target_link_libraries(stm32u083_platform PUBLIC
    HAL::STM32::U0::GPIO
    HAL::STM32::U0::RCC
    HAL::STM32::U0::UART)
