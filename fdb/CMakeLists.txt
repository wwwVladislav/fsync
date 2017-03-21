cmake_minimum_required(VERSION 3.1)
project(fdb)

set(FDB_HEADERS
    src/sync/files.h
    src/db.h
    src/config.h
)

set(FDB_SOURCES
    src/sync/files.c
    src/db.c
    src/config.c
)

use_c99()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
find_package(LIBLMDB REQUIRED)

include_directories(
    ${LIBLMDB_INCLUDES}
    ../futils/include
    ../fcommon/include
)

message(STATUS "LIBLMDB_INCLUDES = ${LIBLMDB_INCLUDES}")

add_library(fdb SHARED ${FDB_HEADERS} ${FDB_SOURCES})
add_dependencies(fdb futils)
target_link_libraries(fdb futils Threads::Threads ${LIBLMDB_LIBRARIES})
