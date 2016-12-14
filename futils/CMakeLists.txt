cmake_minimum_required(VERSION 2.6)
project(futils)

set(FUTILS_HEADERS
    src/errno.h
    src/log.h
    src/prefix_tree.h
    src/queue.h
    src/static_allocator.h
    src/static_assert.h
)

set(FUTILS_SOURCES
    src/log.c
    src/prefix_tree.c
    src/queue.c
    src/static_allocator.c
)

use_c99()

add_library(futils STATIC ${FUTILS_HEADERS} ${FUTILS_SOURCES})
target_link_libraries(futils)
