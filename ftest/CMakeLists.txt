cmake_minimum_required(VERSION 3.1)
project(ftest)

set(FTEST_HEADERS
    src/test.h
)

set(FTEST_SOURCES
    src/main.c
    src/fdb.c
    src/fsync.c
    src/futils.c
)

include_directories(
    ${BINN_INCLUDES}
    ../fdb/include
    ../futils/include
    ../fcommon/include
    ../fsync/include
)

add_executable(ftest ${FTEST_HEADERS} ${FTEST_SOURCES})
add_dependencies(ftest fdb futils fsync binn)
target_link_libraries(ftest fdb futils fsync binn)
