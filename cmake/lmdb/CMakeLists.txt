cmake_minimum_required(VERSION 3.1)
project(lmdb)

set(LMDB_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../libs/lmdb/libraries/liblmdb)
set(LIBLMDB_INCLUDES ${LMDB_ROOT} PARENT_SCOPE)

set(LMDB_HEADERS
    ${LMDB_ROOT}/lmdb.h
    ${LMDB_ROOT}/midl.h
)

set(LMDB_SOURCES
    ${LMDB_ROOT}/mdb.c
    ${LMDB_ROOT}/midl.c
)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(MSVC)
    add_definitions(-FI stdafx.h)
else()
    # GCC or Clang
    add_definitions(-include stdafx.h)
endif()

add_library(lmdb SHARED ${LMDB_HEADERS} ${LMDB_SOURCES})

if(WIN32)
    target_link_libraries(lmdb ntdll)
endif()
