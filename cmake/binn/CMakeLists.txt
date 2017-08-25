cmake_minimum_required(VERSION 3.1)
project(binn)

set(BINN_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../libs/binn)
set(BINN_INCLUDES ${BINN_ROOT}/src PARENT_SCOPE)

set(BINN_HEADERS
    ${BINN_ROOT}/src/binn.h
)

set(BINN_SOURCES
    ${BINN_ROOT}/src/binn.c
)

if (CMAKE_COMPILER_IS_GNUCXX)
    add_definitions(-Wformat=0)
endif()

add_library(binn SHARED ${BINN_HEADERS} ${BINN_SOURCES})
