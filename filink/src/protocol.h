#ifndef PROTOCOL_H_FILINK
#define PROTOCOL_H_FILINK
#include <futils/uuid.h>
#include <fnet/transport.h>
#include <stdbool.h>

// Message types
typedef enum
{
    FPROTO_HELLO = 0,           // Handshake
    FPROTO_NODE_STATUS          // Node status notification
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
    FPROTO_STATUS_READY4SYNC    = 1 << 0    // Node is ready for files synchronization
};

typedef struct
{
    fuuid_t  uuid;
    uint32_t status;
} fproto_node_status_t;

// Message handlers
typedef void (*fproto_node_status_handler_t)(void *, fuuid_t const *, uint32_t);

typedef struct
{
    void                           *param;
    fproto_node_status_handler_t    node_status_handler;
} fproto_msg_handlers_t;

bool fproto_client_handshake_request (fnet_client_t *client, fuuid_t const *uuid, fuuid_t *peer_uuid);
bool fproto_client_handshake_response(fnet_client_t *client, fuuid_t const *uuid, fuuid_t *peer_uuid);
bool fproto_send                     (fnet_client_t *client, fproto_msg_t msg, uint8_t const *data);
bool fproto_read_message             (fnet_client_t *client, fproto_msg_handlers_t *handlers);

#endif
