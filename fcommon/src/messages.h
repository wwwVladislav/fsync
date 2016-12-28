#ifndef MESSAGES_H_FCOMMON
#define MESSAGES_H_FCOMMON
#include <futils/uuid.h>
#include <futils/md5.h>

typedef enum
{
    FNODE_STATUS = 1,
    FSYNC_FILES_LIST,
    FREQUEST_FILES_CONTENT,
    FFILE_INFO,
    FFILE_PART
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

typedef struct
{
    fuuid_t             uuid;
    fuuid_t             destination;
    uint8_t             files_num;
    char                files[32][FMAX_PATH];
} fmsg_request_files_content_t;

typedef struct
{
    fuuid_t             uuid;
    fuuid_t             destination;
    char                path[FMAX_PATH];
    uint32_t            id;
    uint64_t            size;
} fmsg_file_info_t;

typedef struct
{
    fuuid_t             uuid;
    fuuid_t             destination;
    uint32_t            id;
    uint16_t            size;
    uint64_t            offset;
    uint8_t             data[64 * 1024];
} fmsg_file_part_t;

#endif
