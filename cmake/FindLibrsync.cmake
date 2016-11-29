# - Check for the presence of librsync
#
# The following variables are set when LIBRSYNC is found:
#  LIBRSYNC_FOUND           - Set to true, if all components of LIBRSYNC have been found.
#  LIBRSYNC_INCLUDES        - Include path for the header files of LIBRSYNC
#  LIBRSYNC_LIBRARIES       - Link these to use LIBRSYNC

INCLUDE(FindPackageHandleStandardArgs)

if (NOT LIBRSYNC_FOUND)
    if (NOT LIBRSYNC_ROOT_DIR)
        set(_x86 " (x86)")
        set (LIBRSYNC_ROOT_DIR
            ${CMAKE_INSTALL_PREFIX}
            $ENV{ProgramFiles}/librsync
            $ENV{ProgramFiles}${_x86}/librsync
            ../libs/librsync
        )
    endif (NOT LIBRSYNC_ROOT_DIR)

    # Find the header files
    find_path (LIBRSYNC_INCLUDES librsync.h
        HINTS ${LIBRSYNC_ROOT_DIR}
        PATH_SUFFIXES include
    )

    # Find the library
    find_library (LIBRSYNC_LIBRARIES librsync
        HINTS ${LIBRSYNC_ROOT_DIR}
        PATH_SUFFIXES lib
    )

    # Actions taken when all components have been found
    FIND_PACKAGE_HANDLE_STANDARD_ARGS (LIBRSYNC DEFAULT_MSG LIBRSYNC_INCLUDES LIBRSYNC_LIBRARIES)

    if (LIBRSYNC_FOUND)
        if (NOT LIBRSYNC_FIND_QUIETLY)
            message (STATUS "Found components for LIBRSYNC")
            message (STATUS "LIBRSYNC_ROOT_DIR  = ${LIBRSYNC_ROOT_DIR}")
            message (STATUS "LIBRSYNC_INCLUDES  = ${LIBRSYNC_INCLUDES}")
            message (STATUS "LIBRSYNC_LIBRARIES = ${LIBRSYNC_LIBRARIES}")
        endif (NOT LIBRSYNC_FIND_QUIETLY)
    else (LIBRSYNC_FOUND)
        if (LIBRSYNC_FIND_REQUIRED)
            message (FATAL_ERROR "Could not find LIBRSYNC!")
        endif (LIBRSYNC_FIND_REQUIRED)
    endif (LIBRSYNC_FOUND)

    # Mark advanced variables
    mark_as_advanced (
        LIBRSYNC_ROOT_DIR
        LIBRSYNC_INCLUDES
        LIBRSYNC_LIBRARIES
    )

endif (NOT LIBRSYNC_FOUND)
