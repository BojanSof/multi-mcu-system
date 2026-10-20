cmake_minimum_required(VERSION 3.20)
project(host_control_tests C)
enable_testing()
add_executable(host_control_test HostControlTest.c)
target_compile_features(host_control_test PRIVATE c_std_11)
target_compile_options(host_control_test PRIVATE -Wall -Wextra -Werror -Wno-unused-function -UNDEBUG)
target_include_directories(host_control_test PRIVATE . ../include
    ../../../libraries/node-api/include ../../../examples/virtual-io/include)
add_test(NAME host_control_state COMMAND host_control_test)

# Optional integration against the exact CBOR implementation from the pinned
# SDK. Pure state tests above remain available without any Nordic checkout.
get_filename_component(MULTI_MCU_TEST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(HOST_CONTROL_NCS_ROOT "${MULTI_MCU_TEST_ROOT}/.ncs-workspace" CACHE PATH "Pinned NCS workspace")
set(HOST_CONTROL_ZCBOR "${HOST_CONTROL_NCS_ROOT}/modules/lib/zcbor")
set(HOST_CONTROL_MCUMGR "${HOST_CONTROL_NCS_ROOT}/zephyr/subsys/mgmt/mcumgr")
if(EXISTS "${HOST_CONTROL_ZCBOR}/src/zcbor_encode.c")
    add_executable(host_control_cbor_test HostControlCborTest.c
        "${HOST_CONTROL_ZCBOR}/src/zcbor_common.c"
        "${HOST_CONTROL_ZCBOR}/src/zcbor_encode.c"
        "${HOST_CONTROL_ZCBOR}/src/zcbor_decode.c"
        "${HOST_CONTROL_MCUMGR}/util/src/zcbor_bulk.c")
    target_compile_features(host_control_cbor_test PRIVATE c_std_11)
    # The upstream key macro initializes uint8_t* with a string literal, and
    # zcbor's non-verbose build has an unused diagnostic temporary.
    target_compile_options(host_control_cbor_test PRIVATE -Wall -Wextra -Werror
        -Wno-unused-function -Wno-pointer-sign -Wno-unused-but-set-variable -UNDEBUG)
    target_include_directories(host_control_cbor_test PRIVATE . stubs ../include
        ../../../libraries/node-api/include ../../../examples/virtual-io/include
        "${HOST_CONTROL_ZCBOR}/include" "${HOST_CONTROL_MCUMGR}/util/include")
    add_test(NAME host_control_real_cbor COMMAND host_control_cbor_test)
endif()
