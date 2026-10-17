include(${CMAKE_CURRENT_LIST_DIR}/../cmake/Sources.cmake)
zephyr_library()
zephyr_library_sources(${FIRMWARE_IMAGE_SOURCES})
zephyr_include_directories(${CMAKE_CURRENT_LIST_DIR}/../include)
