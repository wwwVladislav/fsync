cmake_minimum_required(VERSION 2.6)
project(fsync)

set(FSYNC_HEADERS
    src/config.h
    src/device.h
    src/fsutils.h
    src/sync.h
)

set(FSYNC_SOURCES
    src/fsutils.c
    src/sync.c
)

set(FSYNC_SOURCES
    ${FSYNC_SOURCES}
    src/os/windows/device.c
    src/os/windows/win_fsutils.c
)

find_package(LIBRSYNC REQUIRED)

message(STATUS "LIBRSYNC_INCLUDES = ${LIBRSYNC_INCLUDES}")

include_directories(
    ${LIBRSYNC_INCLUDES}
    ../futils/include
    ../fcommon/include
)

use_c99()

add_library(fsync SHARED ${FSYNC_HEADERS} ${FSYNC_SOURCES})
add_dependencies(fsync futils)
target_link_libraries(fsync futils ${LIBRSYNC_LIBRARIES})
