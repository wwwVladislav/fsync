#ifndef LOG_H_FUTILS
#define LOG_H_FUTILS

typedef enum fslog_level
{
    FS_INFO = 0,
    FS_WARNING,
    FS_ERROR
} fslog_level_t;

void fs_log(fslog_level_t, char const *, int, char const *, char const *, ...);

#define FS_INFO(...) fs_log(FS_INFO,    __FILE__, __LINE__, __func__, __VA_ARGS__)
#define FS_WARN(...) fs_log(FS_WARNING, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define FS_ERR(...)  fs_log(FS_ERROR,   __FILE__, __LINE__, __func__, __VA_ARGS__)

#endif
