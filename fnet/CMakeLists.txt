cmake_minimum_required(VERSION 2.6)
project(fnet)

set(FNET_HEADERS
    src/socket.h
    src/ip_address.h
    src/transport.h
)

set(FNET_SOURCES
    src/transport.c
)

if (WIN32)
    set(FNET_SOURCES
        ${FNET_SOURCES}
        src/os/windows/socket.c
    )
endif (WIN32)

find_package(OpenSSL REQUIRED)

message (STATUS "OPENSSL_INCLUDE_DIR = ${OPENSSL_INCLUDE_DIR}")

include_directories(
    ${OPENSSL_INCLUDE_DIR}
    ../futils/include
)

set(FNET_LIBS
    futils
    ${OPENSSL_LIBRARIES}
)

if (WIN32)
    set(FNET_LIBS
        ${FNET_LIBS}
        ws2_32
    )
endif (WIN32)

use_c99()

add_library(fnet SHARED ${FNET_HEADERS} ${FNET_SOURCES})
add_dependencies(fnet futils)
target_link_libraries(fnet ${FNET_LIBS})
