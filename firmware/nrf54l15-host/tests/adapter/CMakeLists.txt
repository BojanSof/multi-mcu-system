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
endif()
