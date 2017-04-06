cmake_minimum_required(VERSION 3.1)
project(binn)

set(BINN_ROOT ../../libs/binn)

set(BINN_HEADERS
    ${BINN_ROOT}/src/binn.h
)

set(BINN_SOURCES
    ${BINN_ROOT}/src/binn.c
)

add_library(binn SHARED ${BINN_HEADERS} ${BINN_SOURCES})
