#ifndef MESSAGES_H_FCOMMON
#define MESSAGES_H_FCOMMON
#include "limits.h"
#include <futils/uuid.h>
#include <futils/msgbus.h>
#include <futils/md5.h>

typedef enum
{
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // Nodes
    FNODE_STATUS = 1,               // node_status
    FNODE_CONNECTED,                // node_connected
    FNODE_DISCONNECTED,             // node_disconnected

    FSYNC_FILES_LIST,               // sync_files_list
    FFILE_PART_REQUEST,             // file_part_request
    FFILE_PART,                     // file_part

    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // Remote streams
    FSTREAM,                        // stream
    FSTREAM_ACCEPT,                 // stream_accept
    FSTREAM_REJECT,                 // stream_reject
    FSTREAM_FAILED,                 // stream_failed
    FSTREAM_CLOSED,                 // stream_closed
    FSTREAM_DATA,                   // stream_data

    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // Synchronization
    FSYNC_REQUEST,                  // sync_request
    FSYNC_FAILED                    // sync_failed
} fmessage_t;

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Nodes

enum
{
    FSTATUS_R4S_DIRS    = 1 << 0,   // Node is ready for directories synchronization
    FSTATUS_R4S_FILES   = 1 << 1    // Node is ready for files synchronization
};

FMSG_DEF(node_status,
    uint32_t status;
)

FMSG_DEF(node_connected,
)

FMSG_DEF(node_disconnected,
)

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//

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

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Remote streams

FMSG_DEF(stream,
    uint32_t                stream_id;                      // stream id
    uint32_t                metainf_size;                   // meta information size
    uint8_t                 metainf[FMAX_METAINF_SIZE];     // meta information
)

FMSG_DEF(stream_accept,
    uint32_t                stream_id;                      // stream id
)

FMSG_DEF(stream_reject,
    uint32_t                stream_id;                      // stream id
)

FMSG_DEF(stream_failed,
    uint32_t                stream_id;                      // stream id
    uint32_t                err;                            // error code
    char                    msg[FMAX_ERROR_MSG_LEN];        // error message
)

FMSG_DEF(stream_closed,
    uint32_t                stream_id;                      // stream id
)

FMSG_DEF(stream_data,
    uint32_t                stream_id;                      // stream id
    uint64_t                offset;                         // offset
    uint16_t                size;                           // data size
    uint8_t                 data[FSYNC_BLOCK_SIZE];         // data
)

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Synchronization

FMSG_DEF(sync_request,
    uint32_t                listener_id;                    // synchronization listener id
    uint32_t                sync_id;                        // synchronization id
    uint32_t                metainf_size;                   // meta information size
    uint8_t                 metainf[FMAX_METAINF_SIZE];     // meta information
)

FMSG_DEF(sync_failed,
    uint32_t                listener_id;                    // synchronization listener id
    uint32_t                sync_id;                        // synchronization id
    uint32_t                err;                            // error code
    char                    msg[FMAX_ERROR_MSG_LEN];        // error message
)

#endif
