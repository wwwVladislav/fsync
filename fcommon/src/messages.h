#ifndef MESSAGES_H_FCOMMON
#define MESSAGES_H_FCOMMON
#include "limits.h"
#include <futils/uuid.h>
#include <futils/md5.h>

typedef enum
{
    FNODE_STATUS = 1,
    FSYNC_FILES_LIST,
    FFILE_PART_REQUEST,
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

typedef struct
{
    uint32_t id;
    char     path[FMAX_PATH];
    fmd5_t   digest;
    uint64_t size;
    bool     is_exist;
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
    uint32_t            id;
    uint32_t            block_number;
} fmsg_file_part_request_t;

typedef struct
{
    fuuid_t             uuid;
    fuuid_t             destination;
    uint32_t            id;
    uint32_t            block_number;
    uint16_t            size;
    uint8_t             data[FSYNC_BLOCK_SIZE];
} fmsg_file_part_t;

#endif
