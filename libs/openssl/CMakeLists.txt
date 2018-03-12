cmake_minimum_required(VERSION 3.4)
project(openssl)

include_directories(
    include
)

set(BUILD_OBJECT_LIBRARY_ONLY OFF)

include_directories(
    BEFORE SYSTEM
    ${CMAKE_CURRENT_BINARY_DIR}/crypto
    ${CMAKE_CURRENT_BINARY_DIR}/ssl
    openssl/crypto
    openssl
    include
)

add_definitions( -DOPENSSL_NO_ASM )

if( WIN32 AND NOT CYGWIN )
  add_definitions( -DOPENSSL_SYSNAME_WIN32 )
  add_definitions( -DWIN32_LEAN_AND_MEAN )
endif ( )

if( MINGW )
  set( CMAKE_SHARED_LINKER_FLAGS "-Wl,--export-all" )
endif()

add_definitions(-fPIC)

set(OBJECTS_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/x509_vpm.c
)

add_subdirectory( openssl/crypto crypto )
add_subdirectory( openssl/ssl    ssl )
add_subdirectory( openssl/apps   apps )
