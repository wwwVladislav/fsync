#include "log.h"
#include <stdio.h>
#include <stdarg.h>

void fs_log(fslog_level_t level, char const *file, int line, char const *func, char const *fmt, ...)
{
    (void)level;
    (void)file;
    (void)line;
    (void)func;
    (void)fmt;

    va_list arglist;
    va_start( arglist, fmt );
    vprintf( fmt, arglist );
    va_end( arglist );
    printf("\n");
}
