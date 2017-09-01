#ifndef LIMITS_H_FCOMMON
#define LIMITS_H_FCOMMON

enum
{
    FMAX_CONNECTIONS_NUM        = 128,      // Maximum allowed connections
    FMSGBUS_THREADS_NUM         = 8,        // Threads number for messages handling
    FDATA_SYNC_THREADS_NUM      = 4,        // Threads number for data synchronization
    FMAX_PATH                   = 1024,     // Max file path length
    FMAX_FILENAME               = 260,      // Max file name length
    FMAX_DIR_DEPTH              = 512,      // Max directories depth
    FSYNC_TIMEOUT               = 10,       // Timeout for file synchronization after changing (sec)
    FMAX_ACCEPT_CONNECTIONS     = 10,       // Maximum number of accepted connections
    FSYNC_BLOCK_SIZE            = 4 * 1024, // synchronization blocks size
    FSYNC_BLOCK_REQ_TIMEOUT     = 24*60*60, // Timeout for file part request (sec)
    FSYNC_MAX_REQ_PARTS_NUM     = 64,       // Maximum number of requested parts
    FMAX_ADDR                   = 1024,     // Max address length
    FMAX_METAINF_SIZE           = 512,      // Maximum size of meta information
    FMAX_ERROR_MSG_LEN          = 256,      // Maximum length of error message
};

#endif
