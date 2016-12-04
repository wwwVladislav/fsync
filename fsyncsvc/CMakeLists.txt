cmake_minimum_required(VERSION 2.6)
project(fsyncsvc)

set(FSYNCSVC_HEADERS
)

set(FSYNCSVC_SOURCES
    src/main.c
)

include_directories(../fsync/include)

add_executable(fsyncsvc ${FSYNCSVC_HEADERS} ${FSYNCSVC_SOURCES})
add_dependencies(fsyncsvc fsync fiface)
target_link_libraries(fsyncsvc fsync fiface)
