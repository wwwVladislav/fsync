cmake_minimum_required(VERSION 3.1)
project(fsync)

set(FSYNC_HEADERS
    src/device.h
    src/fsync.h
    src/rsync.h
    src/synchronizer.h
    src/file_assembler.h
    src/search_engine.h
    src/search_engine_sync_agent.h
    src/sync_agents.h
    src/sync_engine.h
    src/rstream.h
)

set(FSYNC_SOURCES
    src/fsync.c
    src/rsync.c
    src/synchronizer.c
    src/file_assembler.c
    src/search_engine.c
    src/search_engine_sync_agent.c
    src/sync_engine.c
    src/rstream.c
)

set(FSYNC_SOURCES
    ${FSYNC_SOURCES}
    src/os/windows/device.c
)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
find_package(LIBRSYNC REQUIRED)

message(STATUS "LIBRSYNC_INCLUDES = ${LIBRSYNC_INCLUDES}")

include_directories(
    ${LIBRSYNC_INCLUDES}
    ${BINN_INCLUDES}
    ../futils/include
    ../fcommon/include
    ../fdb/include
)

use_c99()

if (CMAKE_COMPILER_IS_GNUCXX)
    add_definitions(-pedantic)
endif ()

add_library(fsync SHARED ${FSYNC_HEADERS} ${FSYNC_SOURCES})
add_dependencies(fsync futils fdb binn)
target_link_libraries(fsync futils fdb binn ${LIBRSYNC_LIBRARIES} Threads::Threads)
