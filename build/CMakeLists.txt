# usage:
# cmake -DCMAKE_BUILD_TYPE=Release CMakeLists.txt
# cmake -DCMAKE_BUILD_TYPE=Debug CMakeLists.txt
# cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo CMakeLists.txt
# cmake -DCMAKE_BUILD_TYPE=MinSizeRel CMakeLists.txt
# cmake -DCMAKE_BUILD_TYPE=Debug -G "MinGW Makefiles" .

cmake_minimum_required(VERSION 3.1)
project(fsync)

set(FSYNC_MAJOR_VERSION 1)
set(FSYNC_MINOR_VERSION 0)
set(FSYNC_PATCH_VERSION 0)

set(FSYNC_VERSION ${FSYNC_MAJOR_VERSION}.${FSYNC_MINOR_VERSION}.${FSYNC_PATCH_VERSION})

#*********************************************************
# set build type
#*********************************************************
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
endif()

#*********************************************************
# module path
#*********************************************************
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../cmake")

#*********************************************************
# output directory
#*********************************************************
set(FSYNC_OUTPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../output)

#*********************************************************
# BITNESS
#*********************************************************
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(BITNESS 64)
    add_definitions (-DFSYNC_X64)
    message("Bitness: 64 bit")
else()
    set(BITNESS 32)
    add_definitions (-DFSYNC_X86)
    message("Bitness: 32 bit")
endif()

#*********************************************************
# common compiler flags
#*********************************************************
if (MSVC)
    add_definitions(/W4)
elseif (CMAKE_COMPILER_IS_GNUCXX)
    add_definitions (-Wall)
else ()
    message ("Unknown compiler")
endif ()

#*********************************************************
# output file prefixes
#*********************************************************
if(BITNESS EQUAL 32)
    set(FSYNC_RELEASE_POSTFIX        "")
    set(FSYNC_DEBUG_POSTFIX          "-d")
    set(FSYNC_MINSIZEREL_POSTFIX     "-rm")
    set(FSYNC_RELWITHDEBINFO_POSTFIX "-rd")
else()
    set(FSYNC_RELEASE_POSTFIX        "64")
    set(FSYNC_DEBUG_POSTFIX          "64-d")
    set(FSYNC_MINSIZEREL_POSTFIX     "64-rm")
    set(FSYNC_RELWITHDEBINFO_POSTFIX "64-rd")
endif()

set(CMAKE_RELEASE_POSTFIX           ${FSYNC_RELEASE_POSTFIX})
set(CMAKE_DEBUG_POSTFIX             ${FSYNC_DEBUG_POSTFIX})
set(CMAKE_MINSIZEREL_POSTFIX        ${FSYNC_MINSIZEREL_POSTFIX})
set(CMAKE_RELWITHDEBINFO_POSTFIX    ${FSYNC_RELWITHDEBINFO_POSTFIX})

#*********************************************************
# output directories
#*********************************************************
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${FSYNC_OUTPUT_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${FSYNC_OUTPUT_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${FSYNC_OUTPUT_DIR})

#*********************************************************
# useful macros
#*********************************************************
macro(use_c99)
  if (CMAKE_VERSION VERSION_LESS "3.1")
    if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
      set (CMAKE_C_FLAGS "--std=gnu99 ${CMAKE_C_FLAGS}")
    endif ()
  else ()
    set (CMAKE_C_STANDARD 99)
  endif ()
endmacro(use_c99)

add_subdirectory(${CMAKE_MODULE_PATH}/binn      binn)
add_subdirectory(${CMAKE_MODULE_PATH}/lmdb      lmdb)
add_subdirectory(${CMAKE_MODULE_PATH}/openssl   openssl)
add_subdirectory(${CMAKE_MODULE_PATH}/rsync     rsync)
add_subdirectory(../futils                      futils)
add_subdirectory(../fdb                         fdb)
add_subdirectory(../fsync                       fsync)
add_subdirectory(../fnet                        fnet)
add_subdirectory(../filink                      filink)
add_subdirectory(../fclient                     fclient)
add_subdirectory(../ftest                       ftest)
add_subdirectory(../ftools/fdbtool              fdbtool)
