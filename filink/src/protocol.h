#ifndef PROTOCOL_H_FILINK
#define PROTOCOL_H_FILINK
#include <futils/uuid.h>
#include <futils/md5.h>
#include <fnet/transport.h>
#include <stdbool.h>

// Message types
typedef enum
{
    FPROTO_HELLO = 0,               // Handshake
    FPROTO_NODE_STATUS,             // Node status notification
    FPROTO_SYNC_FILES_LIST,         // Files list notification for sync
    FPROTO_REQUEST_FILES_CONTENT,   // Request files content
    FPROTO_FILE_INFO,               // File information
    FPROTO_FILE_PART                // File part
} fproto_msg_t;

enum
{
    FPROTO_VERSION = 1          // Protocol version 1
};

// FPROTO_HELLO
typedef struct
{
    fuuid_t  uuid;              // node uuid
    uint32_t version;           // protocol version
} fproto_hello_t;

// FPROTO_NODE_STATUS
enum
{
    FPROTO_STATUS_READY4SYNC = 1 << 0    // Node is ready for files synchronization
};

typedef struct
{
    fuuid_t  uuid;
    uint32_t status;
} fproto_node_status_t;

// FPROTO_FILES_LIST
enum
{
    FPROTO_MAX_PATH   = 1024,           // Max file path length
    FPROTO_SYNC_FILES_LIST_SIZE = 32
};

typedef struct
{
    char    path[FPROTO_MAX_PATH];
    fmd5_t  digest;
} fproto_sync_file_info_t;

typedef struct
{
    fuuid_t                 uuid;
    bool                    is_last;
    uint8_t                 files_num;
    fproto_sync_file_info_t files[FPROTO_SYNC_FILES_LIST_SIZE];
} fproto_sync_files_list_t;

typedef struct
{
    fuuid_t             uuid;
    uint8_t             files_num;
    char                files[FPROTO_SYNC_FILES_LIST_SIZE][FPROTO_MAX_PATH];
} fproto_request_files_content_t;

typedef struct
{
    fuuid_t             uuid;
    char                path[FPROTO_MAX_PATH];
    uint32_t            id;
    uint64_t            size;
} fproto_file_info_t;

typedef struct
{
    fuuid_t             uuid;
    uint32_t            id;
    uint16_t            size;
    uint64_t            offset;
    uint8_t             data[64 * 1024];
} fproto_file_part_t;

// Message handlers
typedef void (*fproto_node_status_handler_t)(void *, fuuid_t const *, uint32_t);
typedef void (*fproto_sync_files_list_handler_t)(void *, fuuid_t const *, bool, uint8_t, fproto_sync_file_info_t const *);
typedef void (*fproto_request_files_content_handler_t)(void *, fuuid_t const *, uint8_t, char (*)[FPROTO_MAX_PATH]);
typedef void (*fproto_file_info_handler_t)(void *, fuuid_t const *, char const *, uint32_t, uint64_t);
typedef void (*fproto_file_part_handler_t)(void *, fuuid_t const *, uint32_t, uint16_t, uint64_t, uint8_t const *);

typedef struct
{
    void                                  *param;
    fproto_node_status_handler_t           node_status_handler;
    fproto_sync_files_list_handler_t       sync_files_list_handler;
    fproto_request_files_content_handler_t request_files_content_handler;
    fproto_file_info_handler_t             file_info_handler;
    fproto_file_part_handler_t             file_part_handler;
} fproto_msg_handlers_t;

bool fproto_client_handshake_request (fnet_client_t *client, fuuid_t const *uuid, fuuid_t *peer_uuid);
bool fproto_client_handshake_response(fnet_client_t *client, fuuid_t const *uuid, fuuid_t *peer_uuid);
bool fproto_send                     (fnet_client_t *client, fproto_msg_t msg, uint8_t const *data);
bool fproto_read_message             (fnet_client_t *client, fproto_msg_handlers_t *handlers);

#endif
