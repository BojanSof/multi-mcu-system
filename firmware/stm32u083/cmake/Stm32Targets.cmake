include_guard(GLOBAL)

function(multi_mcu_configure_stm32_application target)
    target_compile_features(${target} PRIVATE c_std_11)
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow
        -ffunction-sections -fdata-sections)
    target_link_options(${target} PRIVATE -Wl,--gc-sections -Wl,-Map=${target}.map)
    target_link_libraries(${target} PRIVATE
        Stm32U083::Platform
        CMSIS::STM32::U083RC
        HAL::STM32::U0::CORTEX
        HAL::STM32::U0::GPIO
        HAL::STM32::U0::RCC
        HAL::STM32::U0::UART
        FreeRTOS::ARM_CM0
        STM32::Nano
        STM32::NoSys)
endfunction()
