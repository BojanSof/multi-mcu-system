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
    add_executable(host_metrics_test HostMetricsTest.c
        "${HOST_CONTROL_ZCBOR}/src/zcbor_common.c"
        "${HOST_CONTROL_ZCBOR}/src/zcbor_encode.c"
        "${HOST_CONTROL_ZCBOR}/src/zcbor_decode.c"
        "${HOST_CONTROL_MCUMGR}/util/src/zcbor_bulk.c")
    target_compile_features(host_metrics_test PRIVATE c_std_11)
    target_compile_options(host_metrics_test PRIVATE -Wall -Wextra -Werror
        -Wno-unused-function -Wno-pointer-sign -Wno-unused-but-set-variable -UNDEBUG)
    get_target_property(METRICS_TEST_INCLUDES host_control_cbor_test INCLUDE_DIRECTORIES)
    target_include_directories(host_metrics_test PRIVATE ${METRICS_TEST_INCLUDES})
    add_test(NAME host_metrics_real_cbor COMMAND host_metrics_test)
endif()
add_subdirectory(adapter)
if(TARGET Protocol::Protocol)
    add_executable(host_manager_metrics_test manager/HostManagerMetricsTest.c)
    target_compile_features(host_manager_metrics_test PRIVATE c_std_11)
    target_compile_options(host_manager_metrics_test PRIVATE -Wall -Wextra -Werror
        -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter -UNDEBUG)
    target_include_directories(host_manager_metrics_test PRIVATE manager/stubs ../include)
    target_link_libraries(host_manager_metrics_test PRIVATE Protocol::Protocol NodeApi::NodeApi
        Diagnostics::Diagnostics VirtualIo::VirtualIo)
    add_test(NAME host_manager_lifetime_metrics COMMAND host_manager_metrics_test)
endif()

add_executable(host_recovery_policy_test HostRecoveryPolicyTest.c ../src/platform/HostRecoveryPolicy.c)
target_compile_features(host_recovery_policy_test PRIVATE c_std_11)
target_compile_options(host_recovery_policy_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
target_include_directories(host_recovery_policy_test PRIVATE ../include)
add_test(NAME host_recovery_policy COMMAND host_recovery_policy_test)
if(TARGET Protocol::Protocol)
    add_executable(host_reliable_event_test manager/HostReliableEventTest.c)
    target_compile_features(host_reliable_event_test PRIVATE c_std_11)
    target_compile_options(host_reliable_event_test PRIVATE -Wall -Wextra -Werror
        -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter -UNDEBUG)
    target_include_directories(host_reliable_event_test PRIVATE manager/stubs ../include)
    target_link_libraries(host_reliable_event_test PRIVATE Protocol::Protocol NodeApi::NodeApi
        Diagnostics::Diagnostics VirtualIo::VirtualIo)
    add_test(NAME host_reliable_event COMMAND host_reliable_event_test)
endif()
if(TARGET host_control_cbor_test)
    add_executable(host_recovery_control_test HostRecoveryControlTest.c
        "${HOST_CONTROL_ZCBOR}/src/zcbor_common.c"
        "${HOST_CONTROL_ZCBOR}/src/zcbor_encode.c"
        "${HOST_CONTROL_ZCBOR}/src/zcbor_decode.c"
        "${HOST_CONTROL_MCUMGR}/util/src/zcbor_bulk.c")
    target_compile_features(host_recovery_control_test PRIVATE c_std_11)
    target_compile_options(host_recovery_control_test PRIVATE -Wall -Wextra -Werror
        -Wno-unused-function -Wno-pointer-sign -Wno-unused-but-set-variable -UNDEBUG)
    target_include_directories(host_recovery_control_test PRIVATE ${METRICS_TEST_INCLUDES})
    add_test(NAME host_recovery_real_cbor COMMAND host_recovery_control_test)
endif()

add_executable(host_signals_test signals/HostSignalsTest.c)
target_include_directories(host_signals_test PRIVATE signals/stubs ../include)
target_compile_features(host_signals_test PRIVATE c_std_11)
target_compile_options(host_signals_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
add_test(NAME host_signals_optional_interrupts COMMAND host_signals_test)
