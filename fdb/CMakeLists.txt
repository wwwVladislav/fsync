cmake_minimum_required(VERSION 3.1)
project(fdb)

set(FDB_HEADERS
    src/sync/files.h
)

set(FDB_SOURCES
    src/sync/files.c
)

include_directories(
    ../futils/include
    ../fcommon/include
)

use_c99()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_library(fdb SHARED ${FDB_HEADERS} ${FDB_SOURCES})
add_dependencies(fdb futils)
target_link_libraries(fdb futils Threads::Threads)
