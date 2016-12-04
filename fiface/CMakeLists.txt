cmake_minimum_required(VERSION 2.6)
project(fiface)

set(FIFACE_HEADERS
)

set(FIFACE_SOURCES
    src/interface.c
)

set(FIFACE_LIBS
    futils
    fnet
)

if (WIN32)
    set(FIFACE_LIBS
        ${FIFACE_LIBS}
        ws2_32
    )
endif (WIN32)

use_c99()

add_library(fiface SHARED ${FIFACE_HEADERS} ${FIFACE_SOURCES})
add_dependencies(fiface futils fnet)
target_link_libraries(fiface ${FIFACE_LIBS})
