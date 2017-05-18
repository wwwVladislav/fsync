cmake_minimum_required(VERSION 3.1)
project(fsync)

set(FSYNC_HEADERS
    src/device.h
    src/sync.h
    src/synchronizer.h
    src/file_assembler.h
    src/search_engine.h
)

set(FSYNC_SOURCES
    src/sync.c
    src/synchronizer.c
    src/file_assembler.c
    src/search_engine.c
)

set(FSYNC_SOURCES
    ${FSYNC_SOURCES}
    src/os/windows/device.c
)

find_package(LIBRSYNC REQUIRED)

message(STATUS "LIBRSYNC_INCLUDES = ${LIBRSYNC_INCLUDES}")

include_directories(
    ${LIBRSYNC_INCLUDES}
    ../futils/include
    ../fcommon/include
    ../fdb/include
)

use_c99()

add_library(fsync SHARED ${FSYNC_HEADERS} ${FSYNC_SOURCES})
add_dependencies(fsync futils fdb)
target_link_libraries(fsync futils fdb ${LIBRSYNC_LIBRARIES})
