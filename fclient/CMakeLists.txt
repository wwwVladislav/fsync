cmake_minimum_required(VERSION 3.1)
project(fclient)

set(FCLIENT_HEADERS
    src/core.h
)

set(FCLIENT_SOURCES
    src/main.c
    src/core.c
)

find_package(LIBRSYNC REQUIRED)

if (CMAKE_COMPILER_IS_GNUCXX)
    add_definitions(-pedantic)
endif ()

include_directories(
    ../fdb/include
    ../fsync/include
    ../futils/include
    ../filink/include
    ../fcommon/include
    ${LIBRSYNC_INCLUDES}
    ${BINN_INCLUDES}
)

add_executable(fclient ${FCLIENT_HEADERS} ${FCLIENT_SOURCES})
add_dependencies(fclient fsync filink fdb)
target_link_libraries(fclient fsync filink fdb ${LIBRSYNC_LIBRARIES})
