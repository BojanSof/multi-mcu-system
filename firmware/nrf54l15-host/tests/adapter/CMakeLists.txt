if(EXISTS "${HOST_CONTROL_ZCBOR}/src/zcbor_encode.c")
    add_executable(host_update_adapter_test HostUpdateAdapterTest.c
        "${HOST_CONTROL_ZCBOR}/src/zcbor_common.c"
        "${HOST_CONTROL_ZCBOR}/src/zcbor_encode.c"
        "${HOST_CONTROL_ZCBOR}/src/zcbor_decode.c"
        "${HOST_CONTROL_MCUMGR}/util/src/zcbor_bulk.c"
        "${MULTI_MCU_TEST_ROOT}/libraries/firmware-image/src/FirmwareImage.c")
    target_compile_features(host_update_adapter_test PRIVATE c_std_11)
    target_compile_options(host_update_adapter_test PRIVATE -Wall -Wextra -Werror
        -Wno-unused-function -Wno-pointer-sign -Wno-unused-but-set-variable -UNDEBUG)
    target_include_directories(host_update_adapter_test PRIVATE .. ../stubs ../../include
        "${MULTI_MCU_TEST_ROOT}/libraries/node-api/include"
        "${MULTI_MCU_TEST_ROOT}/libraries/protocol/include"
        "${MULTI_MCU_TEST_ROOT}/libraries/boot-protocol/include"
        "${MULTI_MCU_TEST_ROOT}/libraries/firmware-image/include"
        "${MULTI_MCU_TEST_ROOT}/examples/virtual-io/include"
        "${HOST_CONTROL_ZCBOR}/include" "${HOST_CONTROL_MCUMGR}/util/include")
    add_test(NAME host_update_adapter_real_cbor COMMAND host_update_adapter_test)
    add_executable(host_rom_adapter_test HostRomAdapterTest.c
        "${HOST_CONTROL_ZCBOR}/src/zcbor_common.c"
        "${HOST_CONTROL_ZCBOR}/src/zcbor_encode.c"
        "${HOST_CONTROL_ZCBOR}/src/zcbor_decode.c"
        "${HOST_CONTROL_MCUMGR}/util/src/zcbor_bulk.c"
        "${MULTI_MCU_TEST_ROOT}/libraries/firmware-image/src/FirmwareImage.c")
    target_compile_features(host_rom_adapter_test PRIVATE c_std_11)
    target_compile_options(host_rom_adapter_test PRIVATE -Wall -Wextra -Werror
        -Wno-unused-function -Wno-pointer-sign -Wno-unused-but-set-variable -UNDEBUG)
    get_target_property(ADAPTER_TEST_INCLUDES host_update_adapter_test INCLUDE_DIRECTORIES)
    target_include_directories(host_rom_adapter_test PRIVATE ${ADAPTER_TEST_INCLUDES}
        "${MULTI_MCU_TEST_ROOT}/libraries/stm32-rom-boot/include")
    add_test(NAME host_rom_adapter_real_cbor COMMAND host_rom_adapter_test)
    add_executable(host_metrics_timing_test HostRomAdapterTest.c
        "${HOST_CONTROL_ZCBOR}/src/zcbor_common.c"
        "${HOST_CONTROL_ZCBOR}/src/zcbor_encode.c"
        "${HOST_CONTROL_ZCBOR}/src/zcbor_decode.c"
        "${HOST_CONTROL_MCUMGR}/util/src/zcbor_bulk.c"
        "${MULTI_MCU_TEST_ROOT}/libraries/firmware-image/src/FirmwareImage.c")
    target_compile_features(host_metrics_timing_test PRIVATE c_std_11)
    target_compile_definitions(host_metrics_timing_test PRIVATE MULTI_MCU_PHASE12_METRICS=1)
    target_compile_options(host_metrics_timing_test PRIVATE -Wall -Wextra -Werror
        -Wno-unused-function -Wno-pointer-sign -Wno-unused-but-set-variable -UNDEBUG)
    target_include_directories(host_metrics_timing_test PRIVATE ${ADAPTER_TEST_INCLUDES}
        "${MULTI_MCU_TEST_ROOT}/libraries/stm32-rom-boot/include")
    add_test(NAME host_metrics_update_timing COMMAND host_metrics_timing_test)
endif()
