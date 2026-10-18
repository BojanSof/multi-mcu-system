cmake_minimum_required(VERSION 3.24)
project(multi_mcu_stm32_bootloader LANGUAGES C ASM)
option(BOOTLOADER_BUILD_NATIVE_TESTS "Build native boot journal tests" OFF)
option(BOOTLOADER_BUILD_TARGET "Build the STM32U083 bare-metal target" OFF)
get_filename_component(MULTI_MCU_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
add_subdirectory("${MULTI_MCU_ROOT}/libraries/boot-protocol" "${CMAKE_CURRENT_BINARY_DIR}/boot-protocol")
add_subdirectory("${MULTI_MCU_ROOT}/libraries/firmware-image" "${CMAKE_CURRENT_BINARY_DIR}/firmware-image")
add_library(stm32u083_boot_journal STATIC src/BootJournal.c)
add_library(Stm32U083::BootJournal ALIAS stm32u083_boot_journal)
target_compile_features(stm32u083_boot_journal PUBLIC c_std_11)
target_include_directories(stm32u083_boot_journal PUBLIC include)
target_compile_options(stm32u083_boot_journal PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
add_library(stm32u083_boot_engine STATIC src/BootEngine.c)
add_library(Stm32U083::BootEngine ALIAS stm32u083_boot_engine)
target_compile_features(stm32u083_boot_engine PUBLIC c_std_11)
target_include_directories(stm32u083_boot_engine PUBLIC include)
target_compile_options(stm32u083_boot_engine PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
target_link_libraries(stm32u083_boot_engine PUBLIC Stm32U083::BootJournal BootProtocol::BootProtocol FirmwareImage::FirmwareImage)
if(BOOTLOADER_BUILD_NATIVE_TESTS)
  enable_testing()
  add_executable(boot_journal_tests tests/BootJournalTests.c)
  target_compile_features(boot_journal_tests PRIVATE c_std_11)
  target_compile_options(boot_journal_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
  target_link_libraries(boot_journal_tests PRIVATE Stm32U083::BootJournal)
  add_test(NAME boot_journal_tests COMMAND boot_journal_tests)
  add_executable(boot_engine_tests tests/BootEngineTests.c)
  target_compile_features(boot_engine_tests PRIVATE c_std_11)
  target_compile_options(boot_engine_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
  target_link_libraries(boot_engine_tests PRIVATE Stm32U083::BootEngine)
  add_test(NAME boot_engine_tests COMMAND boot_engine_tests)
endif()

if(BOOTLOADER_BUILD_TARGET)
  if(NOT CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "BOOTLOADER_BUILD_TARGET requires the STM32 GCC toolchain")
  endif()
  if(NOT BOOTLOADER_PUBLIC_KEY_HEADER)
    message(FATAL_ERROR "Set BOOTLOADER_PUBLIC_KEY_HEADER to an external absolute header path")
  endif()
  list(PREPEND CMAKE_MODULE_PATH "${MULTI_MCU_ROOT}/third_party/stm32-cmake/cmake")
  stm32_fetch_cmsis(U0)
  stm32_fetch_hal(U0)
  find_package(CMSIS COMPONENTS STM32U083RC REQUIRED)
  find_package(HAL COMPONENTS STM32U0 REQUIRED)
  set_property(TARGET CMSIS::STM32::U083RC PROPERTY INTERFACE_LINK_OPTIONS "")
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(layout_dir "${CMAKE_CURRENT_BINARY_DIR}/generated-layout")
  execute_process(COMMAND "${Python3_EXECUTABLE}" "${MULTI_MCU_ROOT}/scripts/generate_flash_layout.py"
    "${MULTI_MCU_ROOT}/firmware/stm32u083/memory/flash_layout.yaml" "${layout_dir}"
    COMMAND_ERROR_IS_FATAL ANY)
  add_library(boot_crypto STATIC
    "${MULTI_MCU_ROOT}/third_party/mbedtls/library/sha256.c"
    src/BootCryptoSupport.c
    "${MULTI_MCU_ROOT}/third_party/micro-ecc/uECC.c")
  target_include_directories(boot_crypto PUBLIC
    "${MULTI_MCU_ROOT}/third_party/mbedtls/include"
    "${MULTI_MCU_ROOT}/third_party/micro-ecc" config)
  target_compile_definitions(boot_crypto PUBLIC MBEDTLS_CONFIG_FILE="mbedtls_config.h"
    uECC_SUPPORTS_secp160r1=0 uECC_SUPPORTS_secp192r1=0 uECC_SUPPORTS_secp224r1=0
    uECC_SUPPORTS_secp256k1=0 uECC_SUPPORTS_secp256r1=1 uECC_PLATFORM=0)
  foreach(portable_target IN ITEMS boot_protocol firmware_image stm32u083_boot_journal
                                   stm32u083_boot_engine boot_crypto)
    target_compile_options(${portable_target} PRIVATE -mthumb -mcpu=cortex-m0plus)
  endforeach()
  add_executable(multi_mcu_stm32_bootloader src/main.c src/BootTarget.c src/BootVerify.c)
  target_sources(multi_mcu_stm32_bootloader PRIVATE
    "${STM32_HAL_U0_PATH}/Src/stm32u0xx_hal_flash_ex.c")
  target_compile_features(multi_mcu_stm32_bootloader PRIVATE c_std_11)
  target_compile_options(multi_mcu_stm32_bootloader PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -ffunction-sections -fdata-sections)
  target_compile_definitions(multi_mcu_stm32_bootloader PRIVATE STM32U0 STM32U083xx
    BOOTLOADER_PUBLIC_KEY_HEADER="${BOOTLOADER_PUBLIC_KEY_HEADER}")
  target_include_directories(multi_mcu_stm32_bootloader PRIVATE include config "${layout_dir}")
  target_link_options(multi_mcu_stm32_bootloader PRIVATE -Wl,--gc-sections
    -Wl,-Map=multi_mcu_stm32_bootloader.map "-T${layout_dir}/bootloader.ld")
  target_link_libraries(multi_mcu_stm32_bootloader PRIVATE Stm32U083::BootEngine boot_crypto
    CMSIS::STM32::U083RC HAL::STM32::U0::CORTEX HAL::STM32::U0::FLASH
    HAL::STM32::U0::GPIO HAL::STM32::U0::PWR HAL::STM32::U0::PWREx
    HAL::STM32::U0::RCC HAL::STM32::U0::RCCEx HAL::STM32::U0::UART
    HAL::STM32::U0::UARTEx STM32::Nano STM32::NoSys)
  stm32_print_size_of_target(multi_mcu_stm32_bootloader)
  add_custom_command(TARGET multi_mcu_stm32_bootloader POST_BUILD COMMAND "${CMAKE_OBJCOPY}"
    -O binary "$<TARGET_FILE:multi_mcu_stm32_bootloader>"
    "${CMAKE_CURRENT_BINARY_DIR}/multi_mcu_stm32_bootloader.bin")
endif()
