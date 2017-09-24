cmake_minimum_required(VERSION 3.1)
project(fdbtool)

set(FDBTOOL_HEADERS
    src/dbtool.h
)

set(FDBTOOL_SOURCES
    src/main.c
    src/dbtool.c
)

if (CMAKE_COMPILER_IS_GNUCXX)
    add_definitions(-pedantic)
endif ()

include_directories(
    ../../fdb/include
    ../../futils/include
    ../../fcommon/include
)

add_executable(fdbtool ${FDBTOOL_HEADERS} ${FDBTOOL_SOURCES})
add_dependencies(fdbtool fdb)
target_link_libraries(fdbtool fdb)
