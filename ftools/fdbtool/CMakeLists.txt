cmake_minimum_required(VERSION 3.1)
project(fdbtool)

set(FDBTOOL_HEADERS
    src/dbtool.h
)

set(FDBTOOL_SOURCES
    src/main.c
    src/dbtool.c
)

include_directories(
    ../../fdb/include
    ../../futils/include
    ../../fcommon/include
)

add_executable(fdbtool ${FDBTOOL_HEADERS} ${FDBTOOL_SOURCES})
add_dependencies(fdbtool fdb)
target_link_libraries(fdbtool fdb)
