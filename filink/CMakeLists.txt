cmake_minimum_required(VERSION 3.1)
project(filink)

set(FILINK_HEADERS
    src/interface.h
    src/protocol.h
)

set(FILINK_SOURCES
    src/interface.c
    src/protocol.c
)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

include_directories(
    ../fdb/include
    ../fnet/include
    ../futils/include
    ../fcommon/include
    ../libs/openssl/include
    ${BIN_DIR}/openssl/openssl/ssl
    ${BIN_DIR}/openssl/openssl/crypto
)

set(FILINK_LIBS
    futils
    fnet
    Threads::Threads
    fdb
    crypto
    ssl
)

if (WIN32)
    set(FILINK_LIBS
        ${FILINK_LIBS}
        ws2_32
    )
endif (WIN32)

use_c99()

add_library(filink SHARED ${FILINK_HEADERS} ${FILINK_SOURCES})
add_dependencies(filink futils fnet fdb crypto ssl)
target_link_libraries(filink ${FILINK_LIBS})
