#include "log.h"
#include <stdio.h>
#include <stdarg.h>

static fslog_level_t log_level = FS_WARNING;

void fs_log_level_set(fslog_level_t level)
{
    log_level = level;
}

void fs_log(fslog_level_t level, char const *file, int line, char const *func, char const *fmt, ...)
{
    (void)level;
    (void)file;
    (void)line;
    (void)func;
    (void)fmt;

    if (level >= log_level)
    {
        va_list arglist;
        va_start( arglist, fmt );
        vprintf( fmt, arglist );
        va_end( arglist );
        printf("\n");
    }
}
