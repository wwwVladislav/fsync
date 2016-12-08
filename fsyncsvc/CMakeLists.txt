cmake_minimum_required(VERSION 2.6)
project(fsyncsvc)

set(FSYNCSVC_HEADERS
)

set(FSYNCSVC_SOURCES
    src/main.c
)

include_directories(../fsync/include)
include_directories(../fiface/include)
include_directories(../filink/include)

add_executable(fsyncsvc ${FSYNCSVC_HEADERS} ${FSYNCSVC_SOURCES})
add_dependencies(fsyncsvc fsync fiface filink)
target_link_libraries(fsyncsvc fsync fiface filink)
