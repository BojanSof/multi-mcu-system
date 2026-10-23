cmake_minimum_required(VERSION 3.24)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(HostStmUpdateTests LANGUAGES C)
    enable_testing()
endif()
get_filename_component(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
if(NOT TARGET BootProtocol::BootProtocol)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    add_subdirectory("${REPO}/libraries/boot-protocol" boot-protocol)
    add_subdirectory("${REPO}/libraries/firmware-image" firmware-image)
endif()
add_executable(host_stm_update_test HostStmUpdateTest.c
    ../../src/bootloader/HostStmUpdate.c
    "${REPO}/firmware/stm32u083/bootloader/src/BootEngine.c"
    "${REPO}/firmware/stm32u083/bootloader/src/BootJournal.c")
target_include_directories(host_stm_update_test PRIVATE ../../include
    "${REPO}/firmware/stm32u083/bootloader/include")
target_link_libraries(host_stm_update_test PRIVATE BootProtocol::BootProtocol FirmwareImage::FirmwareImage)
target_compile_features(host_stm_update_test PRIVATE c_std_11)
target_compile_options(host_stm_update_test PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Werror -UNDEBUG)
add_test(NAME host_stm_update.workflow COMMAND host_stm_update_test)
