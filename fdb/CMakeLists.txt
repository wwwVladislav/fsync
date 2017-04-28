cmake_minimum_required(VERSION 3.1)
project(fdb)

set(FDB_HEADERS
    src/sync/files.h
    src/sync/nodes.h
    src/sync/config.h
    src/sync/ids.h
    src/sync/statuses.h
    src/sync/download_info.h
    src/db.h
)

set(FDB_SOURCES
    src/sync/files.c
    src/sync/nodes.c
    src/sync/config.c
    src/sync/ids.c
    src/sync/statuses.c
    src/sync/download_info.c
    src/db.c
)

use_c99()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

include_directories(
    ${LIBLMDB_INCLUDES}
    ${BINN_INCLUDES}
    ../futils/include
    ../fcommon/include
)

message(STATUS "LIBLMDB_INCLUDES = ${LIBLMDB_INCLUDES}")

add_library(fdb SHARED ${FDB_HEADERS} ${FDB_SOURCES})
add_dependencies(fdb futils lmdb binn)
target_link_libraries(fdb futils lmdb binn Threads::Threads)
