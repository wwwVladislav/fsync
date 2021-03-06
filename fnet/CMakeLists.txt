cmake_minimum_required(VERSION 3.1)
project(fnet)

set(FNET_HEADERS
    src/ip_address.h
    src/transport.h
    src/tcp_transport.h
    src/ssl_transport.h
    src/socket.h
    src/marshaller.h
)

set(FNET_SOURCES
    src/ip_address.c
    src/transport.c
    src/tcp_transport.c
    src/ssl_transport.c
)

if (WIN32)
    set(FNET_SOURCES
        ${FNET_SOURCES}
        src/os/windows/socket.c
        src/os/windows/marshaller.c
    )
else()
    set(FNET_SOURCES
        ${FNET_SOURCES}
        src/os/linux/socket.c
        src/os/linux/marshaller.c
    )
endif (WIN32)

include_directories(
    ../libs/openssl/include
    ${BIN_DIR}/openssl/openssl/ssl
    ${BIN_DIR}/openssl/openssl/crypto
    ../futils/include
    ../fcommon/include
)

set(FNET_LIBS
    futils
    crypto
    ssl
)

if (WIN32)
    set(FNET_LIBS
        ${FNET_LIBS}
        ws2_32
    )
endif (WIN32)

use_c99()

if (CMAKE_COMPILER_IS_GNUCXX)
    add_definitions(-fPIC)
endif ()

add_library(fnet STATIC ${FNET_HEADERS} ${FNET_SOURCES})
add_dependencies(fnet futils crypto ssl)
target_link_libraries(fnet ${FNET_LIBS})
