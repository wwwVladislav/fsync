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
    src/fs.h
    src/stream.h
    src/mutex.h
    src/utils.h
)

set(FUTILS_SOURCES
    src/log.c
    src/md5.c
    src/queue.c
    src/vector.c
    src/static_allocator.c
    src/msgbus.c
    src/fs.c
    src/stream.c
)

set(FUTILS_LIBS
    crypto
    ssl
)

if(WIN32)
    set(FUTILS_SOURCES
        ${FUTILS_SOURCES}
        src/os/windows/uuid.c
        src/os/windows/fs.c
    )

    set(FUTILS_LIBS
        ${FUTILS_LIBS}
        ws2_32
    )
else()
    set(FUTILS_SOURCES
        ${FUTILS_SOURCES}
        src/os/linux/uuid.c
        src/os/linux/fs.c
    )
endif(WIN32)

include_directories(
    ../libs/openssl/include
    ${OUTPUT_DIR}/openssl/openssl/ssl
    ${OUTPUT_DIR}/openssl/openssl/crypto
    ../fcommon/include
)

use_c99()

if (CMAKE_COMPILER_IS_GNUCXX)
    add_definitions(-fPIC)
endif ()

add_library(futils STATIC ${FUTILS_HEADERS} ${FUTILS_SOURCES})
target_link_libraries(futils ${FUTILS_LIBS})
add_dependencies(futils crypto ssl)
