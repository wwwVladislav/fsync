cmake_minimum_required(VERSION 3.1)
project(futils)

set(FUTILS_HEADERS
    src/uuid.h
    src/errno.h
    src/log.h
    src/md5.h
    src/queue.h
    src/vector.h
    src/static_allocator.h
    src/static_assert.h
    src/msgbus.h
)

set(FUTILS_SOURCES
    src/log.c
    src/md5.c
    src/queue.c
    src/vector.c
    src/static_allocator.c
    src/msgbus.c
)

find_package(OpenSSL REQUIRED)

set(FUTILS_LIBS
    ${OPENSSL_LIBRARIES}
)

if (WIN32)
    set(FUTILS_SOURCES
        ${FUTILS_SOURCES}
        src/os/windows/uuid.c
    )

    set(FUTILS_LIBS
        ${FUTILS_LIBS}
        ws2_32
    )
endif (WIN32)

include_directories(
    ${OPENSSL_INCLUDE_DIR}
)

use_c99()

add_library(futils STATIC ${FUTILS_HEADERS} ${FUTILS_SOURCES})
target_link_libraries(futils ${FUTILS_LIBS})
