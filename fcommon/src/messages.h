#ifndef MESSAGES_H_FCOMMON
#define MESSAGES_H_FCOMMON
#include <futils/uuid.h>
#include <futils/md5.h>

typedef enum
{
    FNODE_STATUS = 1,
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
    FMAX_PATH   = 1024              // Max file path length
};

typedef struct
{
    char    path[FMAX_PATH];
    fmd5_t  digest;
} fsync_file_info_t;

typedef struct
{
    fuuid_t             uuid;
    fuuid_t             destination;
    bool                is_last;
    uint8_t             files_num;
    fsync_file_info_t   files[32];
} fmsg_sync_files_list_t;

#endif
