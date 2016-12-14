/*
    Error codes which are used in futils.
*/

#ifndef ERRNO_H_FUTILS
#define ERRNO_H_FUTILS

typedef enum ferr
{
    FSUCCESS     =  0,
    FFAIL        = -1,
    FINVALID_ARG = -2,
    FNO_MEM      = -3
} ferr_t;

#endif
