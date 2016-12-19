#ifndef PROTOCOL_H_FILINK
#define PROTOCOL_H_FILINK
#include <futils/uuid.h>
#include <fnet/transport.h>

// Message types
typedef enum
{
    FPROTO_HELLO = 0
} fproto_msg_t;

enum
{
    FPROTO_VERSION = 1,         // Protocol version 1
    FPROTO_RESPONSE = 1 << 31   // Mask for response
};

// FPROTO_HELLO_REQ
typedef struct
{
    uint32_t version;   // client side protocol version
    fuuid_t uuid;       // client uuid
} fproto_hello_req_t;

// FPROTO_HELLO_RES
typedef struct
{
    uint32_t version;   // server side protocol version
    fuuid_t uuid;       // server uuid
} fproto_hello_res_t;

bool fproto_req_send(fnet_client_t *client, fproto_msg_t msg, void const *req);
bool fproto_req_recv(fnet_client_t *client, fproto_msg_t msg, void *req);
bool fproto_res_send(fnet_client_t *client, fproto_msg_t msg, void const *res);
bool fproto_res_recv(fnet_client_t *client, fproto_msg_t msg, void *res);

bool fproto_client_handshake_request (fnet_client_t *client, fuuid_t const *uuid, fuuid_t *peer_uuid);
bool fproto_client_handshake_response(fnet_client_t *client, fuuid_t const *uuid, fuuid_t *peer_uuid);

#endif
