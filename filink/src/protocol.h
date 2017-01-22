#ifndef PROTOCOL_H_FILINK
#define PROTOCOL_H_FILINK
#include <futils/uuid.h>
#include <futils/md5.h>
#include <fnet/transport.h>
#include <config.h>
#include <stdbool.h>

// Message types
typedef enum
{
    FPROTO_HELLO = 0,               // Handshake
    FPROTO_NODE_STATUS,             // Node status notification
    FPROTO_SYNC_FILES_LIST,         // Files list notification for sync
    FPROTO_FILE_PART_REQUEST,       // File part request
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
    FPROTO_STATUS_READY4SYNC = 1 << 0           // Node is ready for files synchronization
};

typedef struct
{
    fuuid_t  uuid;
    uint32_t status;
} fproto_node_status_t;

// FPROTO_FILES_LIST
enum
{
    FPROTO_MAX_PATH             = FMAX_PATH,    // Max file path length
    FPROTO_SYNC_FILES_LIST_SIZE = 32,
    FPROTO_SYNC_BLOCK_SIZE      = FSYNC_BLOCK_SIZE
};

typedef struct
{
    uint32_t id;
    char     path[FPROTO_MAX_PATH];
    fmd5_t   digest;
    uint64_t size;
    bool     is_exist;
} fproto_sync_file_info_t;

typedef struct
{
    fuuid_t                 uuid;
    bool                    is_last;
    uint8_t                 files_num;
    fproto_sync_file_info_t files[FPROTO_SYNC_FILES_LIST_SIZE];
} fproto_sync_files_list_t;

// FPROTO_FILE_PART_REQUEST
typedef struct
{
    fuuid_t             uuid;
    uint32_t            id;
    uint32_t            block_number;
} fproto_file_part_request_t;

// FPROTO_FILE_PART
typedef struct
{
    fuuid_t             uuid;
    uint32_t            id;
    uint32_t            block_number;
    uint16_t            size;
    uint8_t             data[FPROTO_SYNC_BLOCK_SIZE];
} fproto_file_part_t;

// Message handlers
typedef void (*fproto_node_status_handler_t)       (void *, fproto_node_status_t const *);
typedef void (*fproto_sync_files_list_handler_t)   (void *, fproto_sync_files_list_t const *);
typedef void (*fproto_file_part_request_handler_t) (void *, fproto_file_part_request_t const *);
typedef void (*fproto_file_part_handler_t)         (void *, fproto_file_part_t const *);

typedef struct
{
    void                                  *param;
    fproto_node_status_handler_t           node_status_handler;
    fproto_sync_files_list_handler_t       sync_files_list_handler;
    fproto_file_part_request_handler_t     file_part_request_handler;
    fproto_file_part_handler_t             file_part_handler;
} fproto_msg_handlers_t;

bool fproto_client_handshake_request (fnet_client_t *client, fuuid_t const *uuid, fuuid_t *peer_uuid);
bool fproto_client_handshake_response(fnet_client_t *client, fuuid_t const *uuid, fuuid_t *peer_uuid);
bool fproto_send                     (fnet_client_t *client, fproto_msg_t msg, uint8_t const *data);
bool fproto_read_message             (fnet_client_t *client, fproto_msg_handlers_t *handlers);

#endif
