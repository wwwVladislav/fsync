#include "protocol.h"
#include <futils/log.h>
#include <string.h>

bool fproto_req_send(fnet_client_t *client, fproto_msg_t msg, void const *data)
{
    switch(msg)
    {
        case FPROTO_HELLO:
        {
            uint32_t const cmd = msg;
            fproto_hello_req_t const *req = (fproto_hello_req_t const *)data;
            if (!fnet_send(client, &cmd,          sizeof cmd))          break;
            if (!fnet_send(client, &req->version, sizeof req->version)) break;
            if (!fnet_send(client, &req->uuid,    sizeof req->uuid))    break;
            return true;
        }

        default:
        {
            FS_ERR("Unknown message type");
            break;
        }
    }
    return false;
}

bool fproto_req_recv(fnet_client_t *client, fproto_msg_t msg, void *data)
{
    switch(msg)
    {
        case FPROTO_HELLO:
        {
            fproto_hello_req_t *req = (fproto_hello_req_t *)data;
            if (!fnet_recv(client, &req->version, sizeof req->version)) break;
            if (!fnet_recv(client, &req->uuid,    sizeof req->uuid))    break;
            return true;
        }

        default:
        {
            FS_ERR("Unknown message type");
            break;
        }
    }
    return false;
}

bool fproto_res_send(fnet_client_t *client, fproto_msg_t msg, void const *data)
{
    switch(msg)
    {
        case FPROTO_HELLO:
        {
            uint32_t const cmd = msg | FPROTO_RESPONSE;
            fproto_hello_res_t const *res = (fproto_hello_res_t const *)data;
            if (!fnet_send(client, &cmd,          sizeof cmd))          break;
            if (!fnet_send(client, &res->version, sizeof res->version)) break;
            if (!fnet_send(client, &res->uuid,    sizeof res->uuid))    break;
            return true;
        }

        default:
        {
            FS_ERR("Unknown message type");
            break;
        }
    }
    return false;
}

bool fproto_res_recv(fnet_client_t *client, fproto_msg_t msg, void *data)
{
    switch(msg)
    {
        case FPROTO_HELLO:
        {
            fproto_hello_res_t *res = (fproto_hello_res_t *)data;
            if (!fnet_recv(client, &res->version, sizeof res->version)) break;
            if (!fnet_recv(client, &res->uuid,    sizeof res->uuid))    break;
            return true;
        }

        default:
        {
            FS_ERR("Unknown message type");
            break;
        }
    }
    return false;
}

bool fproto_client_handshake_request(fnet_client_t *client, fuuid_t const *uuid, fuuid_t *peer_uuid)
{
    // I. Send request
    fproto_hello_req_t const req =
    {
        FPROTO_VERSION,
        *uuid
    };
    if (!fproto_req_send(client, FPROTO_HELLO, &req))
        return false;

    // II. Read response
    uint32_t msg;
    if (!fnet_recv(client, &msg, sizeof msg)) return false;
    if ((msg & FPROTO_RESPONSE) == 0) return false;                 // Not response
    msg &= ~FPROTO_RESPONSE;
    if (msg != FPROTO_HELLO) return false;                          // Not response for HELLO message

    fproto_hello_res_t res;
    if (!fproto_res_recv(client, (fproto_msg_t)msg, &res)) return false;
    if (res.version != FPROTO_VERSION) return false;               // Unsupported protocol version
    if (memcmp(&res.uuid, uuid, sizeof *uuid) == 0) return false;  // Client and server are the same node
    *peer_uuid = res.uuid;
    return true;
}

bool fproto_client_handshake_response(fnet_client_t *client, fuuid_t const *uuid, fuuid_t *peer_uuid)
{
    // I. Read request
    uint32_t msg;
    if (!fnet_recv(client, &msg, sizeof msg)) return false;
    if (msg != FPROTO_HELLO) return false;                          // Not HELLO message

    fproto_hello_req_t req;
    if (!fproto_req_recv(client, (fproto_msg_t)msg, &req)) return false;
    if (req.version != FPROTO_VERSION) return false;               // Unsupported protocol version
    if (memcmp(&req.uuid, uuid, sizeof *uuid) == 0) return false;  // Client and server are the same node
    *peer_uuid = req.uuid;

    // II. Send response
    fproto_hello_req_t const res =
    {
        FPROTO_VERSION,
        *uuid
    };
    if (!fproto_res_send(client, FPROTO_HELLO, &res))
        return false;
    return true;
}
