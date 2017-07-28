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

find_package(OpenSSL REQUIRED)

message (STATUS "OPENSSL_INCLUDE_DIR = ${OPENSSL_INCLUDE_DIR}")

include_directories(
    ../fdb/include
    ../fnet/include
    ../futils/include
    ../fcommon/include
)

set(FILINK_LIBS
    futils
    fnet
    Threads::Threads
    ${OPENSSL_LIBRARIES}
)

if (WIN32)
    set(FILINK_LIBS
        ${FILINK_LIBS}
        ws2_32
    )
endif (WIN32)

use_c99()

add_library(filink SHARED ${FILINK_HEADERS} ${FILINK_SOURCES})
add_dependencies(filink futils fnet fdb)
target_link_libraries(filink ${FILINK_LIBS} fdb)
