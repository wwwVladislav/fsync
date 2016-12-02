cmake_minimum_required(VERSION 2.6)
project(fclient)

set(FCLIENT_HEADERS
)

set(FCLIENT_SOURCES
    src/main.c
)

include_directories(
    ../fsync/include
    ../fnet/include
)

add_executable(fclient ${FCLIENT_HEADERS} ${FCLIENT_SOURCES})
add_dependencies(fclient fsync fnet)
target_link_libraries(fclient fsync fnet)
