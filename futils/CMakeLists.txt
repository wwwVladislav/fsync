cmake_minimum_required(VERSION 2.6)
project(futils)

set(FUTILS_HEADERS
    src/log.h
    src/queue.h
)

set(FUTILS_SOURCES
    src/log.c
    src/queue.c
)

use_c99()

add_library(futils STATIC ${FUTILS_HEADERS} ${FUTILS_SOURCES})
target_link_libraries(futils)
