#ifndef CONFIG_H_FCOMMON
#define CONFIG_H_FCOMMON

enum
{
    FMAX_CONNECTIONS_NUM        = 128,      // Maximum allowed connections
    FMAX_PATH                   = 1024,     // Max file path length
    FMAX_FILENAME               = 260,      // Max file name length
    FMAX_DIR_DEPTH              = 512,      // Max directories depth
    FSYNC_TIMEOUT               = 10,       // Timeout for file synchronization after changing (sec)
    FMAX_ACCEPT_CONNECTIONS     = 10,       // Maximum number of accepted connections
    FSYNC_BLOCK_SIZE            = 64 * 1024 // synchronization blocks size
};

#endif
