cmake_minimum_required(VERSION 3.1)
project(rsync)

set(RSYNC_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../libs/rsync)
set(RSYNC_INCLUDES ${RSYNC_ROOT}/src PARENT_SCOPE)

option(BUILD_RDIFF "" OFF)
add_subdirectory(${RSYNC_ROOT}  librsync)
