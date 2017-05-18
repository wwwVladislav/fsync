cmake_minimum_required(VERSION 3.1)
project(ftest)

set(FTEST_HEADERS
)

set(FTEST_SOURCES
    src/main.c
    src/fdb.c
    src/fs.c
)

include_directories(
    ../fdb/include
    ../futils/include
    ../fcommon/include
)

add_executable(ftest ${FTEST_HEADERS} ${FTEST_SOURCES})
add_dependencies(ftest fdb futils)
target_link_libraries(ftest fdb futils)
