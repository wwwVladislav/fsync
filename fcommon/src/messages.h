#ifndef MESSAGES_H_FCOMMON
#define MESSAGES_H_FCOMMON
#include <futils/uuid.h>

typedef enum
{
    FNODE_STATUS = 0,
    FSYNC_FILES_LIST,
} fmessage_t;

enum
{
    FSTATUS_READY4SYNC  = 1 << 0    // Node is ready for files synchronization
};

typedef struct
{
    fuuid_t  uuid;
    uint32_t status;
} fmsg_node_status_t;

enum
{
    FMAX_PATH   = 1024,             // Max file path length
    FDIGEST_LEN = 16                // File digest length
};

typedef struct
{
    char    path[FMAX_PATH];
    uint8_t digest[FDIGEST_LEN];
} fsync_file_info_t;

typedef struct
{
    fuuid_t             uuid;
    bool                is_last;
    uint8_t             files_num;
    fsync_file_info_t   files[1];
} fmsg_sync_files_list_t;

#endif
