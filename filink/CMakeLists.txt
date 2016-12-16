cmake_minimum_required(VERSION 2.6)
project(filink)

set(FILINK_HEADERS
    src/interface.h
    src/protocol.h
)

set(FILINK_SOURCES
    src/interface.c
)

set(FILINK_LIBS
    futils
    fnet
)

include_directories(
    ../fnet/include
    ../futils/include
)

if (WIN32)
    set(FILINK_LIBS
        ${FILINK_LIBS}
        ws2_32
    )
endif (WIN32)

use_c99()

add_library(filink SHARED ${FILINK_HEADERS} ${FILINK_SOURCES})
add_dependencies(filink futils fnet)
target_link_libraries(filink ${FILINK_LIBS})
