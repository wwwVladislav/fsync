#ifndef MESSAGES_H_FCOMMON
#define MESSAGES_H_FCOMMON
#include "limits.h"
#include <futils/uuid.h>
#include <futils/msgbus.h>
#include <futils/md5.h>

typedef enum
{
    FNODE_STATUS = 1,
    FSYNC_FILES_LIST,
    FFILE_PART_REQUEST,
    FFILE_PART,
    FSTREAM_REQUEST,
    FSTREAM,
    FSTREAM_DATA
} fmessage_t;

enum
{
    FSTATUS_R4S_DIRS    = 1 << 0,   // Node is ready for directories synchronization
    FSTATUS_R4S_FILES   = 1 << 1    // Node is ready for files synchronization
};

FMSG_DEF(node_status,
    uint32_t status;
)

typedef struct
{
    uint32_t id;
    char     path[FMAX_PATH];
    fmd5_t   digest;
    uint64_t size;
    bool     is_exist;
} fmsg_sync_file_info_t;

FMSG_DEF(sync_files_list,
    bool                    is_last;
    uint8_t                 files_num;
    fmsg_sync_file_info_t   files[32];
)

FMSG_DEF(file_part_request,
    uint32_t                id;
    uint32_t                block_number;
)

FMSG_DEF(file_part,
    uint32_t                id;
    uint32_t                block_number;
    uint16_t                size;
    uint8_t                 data[FSYNC_BLOCK_SIZE];
)

FMSG_DEF(stream_request,
    uint32_t                cookie;                 // cookie (can contain some useful ID, for example, source component ID)
)

FMSG_DEF(stream,
    uint32_t                id;                     // stream id
    uint32_t                cookie;                 // cookie (can contain some useful ID, for example, source component ID)
)

FMSG_DEF(stream_data,
    uint32_t                id;                     // stream id
    uint64_t                offset;                 // offset
    uint16_t                size;                   // data size
    uint8_t                 data[FSYNC_BLOCK_SIZE]; // data
)

#endif
