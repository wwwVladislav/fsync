/*
    Error codes which are used in futils.
*/

#ifndef ERRNO_H_FUTILS
#define ERRNO_H_FUTILS

typedef enum ferr
{
    FSUCCESS         =  0,
    FFAIL            = -1,
    FERR_INVALID_ARG = -2,
    FERR_NO_MEM      = -3,
    FERR_UNKNOWN     = -4
} ferr_t;

#endif
