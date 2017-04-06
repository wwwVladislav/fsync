# - Check for the presence of lmdb
#
# The following variables are set when LIBLMDB is found:
#  LIBLMDB_FOUND           - Set to true, if all components of LIBLMDB have been found.
#  LIBLMDB_INCLUDES        - Include path for the header files of LIBLMDB
#  LIBLMDB_LIBRARIES       - Link these to use LIBLMDB

INCLUDE(FindPackageHandleStandardArgs)

if (NOT LIBLMDB_FOUND)
    if (NOT LIBLMDB_ROOT_DIR)
        set(_x86 " (x86)")
        set (LIBLMDB_ROOT_DIR
            ${CMAKE_INSTALL_PREFIX}
            $ENV{ProgramFiles}/lmdb
            $ENV{ProgramFiles}${_x86}/lmdb
            ../libs/lmdb
        )
    endif (NOT LIBLMDB_ROOT_DIR)

    # Find the header files
    find_path (LIBLMDB_INCLUDES lmdb.h
        HINTS ${LIBLMDB_ROOT_DIR}
        PATH_SUFFIXES include
    )

    # Find the library
    find_library (LIBLMDB_LIBRARIES liblmdb
        HINTS ${LIBLMDB_ROOT_DIR}
        PATH_SUFFIXES lib
    )

    # Actions taken when all components have been found
    FIND_PACKAGE_HANDLE_STANDARD_ARGS (LIBLMDB DEFAULT_MSG LIBLMDB_INCLUDES LIBLMDB_LIBRARIES)

    if (LIBLMDB_FOUND)
        set (LIBLMDB_LIBRARIES ${LIBLMDB_LIBRARIES} ntdll)

        if (NOT LIBLMDB_FIND_QUIETLY)
            message (STATUS "Found components for LIBLMDB")
            message (STATUS "LIBLMDB_ROOT_DIR  = ${LIBLMDB_ROOT_DIR}")
            message (STATUS "LIBLMDB_INCLUDES  = ${LIBLMDB_INCLUDES}")
            message (STATUS "LIBLMDB_LIBRARIES = ${LIBLMDB_LIBRARIES}")
        endif (NOT LIBLMDB_FIND_QUIETLY)
    else (LIBLMDB_FOUND)
        if (LIBLMDB_FIND_REQUIRED)
            message (FATAL_ERROR "Could not find LIBLMDB!")
        endif (LIBLMDB_FIND_REQUIRED)
    endif (LIBLMDB_FOUND)

    # Mark advanced variables
    mark_as_advanced (
        LIBLMDB_ROOT_DIR
        LIBLMDB_INCLUDES
        LIBLMDB_LIBRARIES
    )

endif (NOT LIBLMDB_FOUND)
