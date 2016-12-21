#include "protocol.h"
#include <futils/log.h>
#include <string.h>
#include <fnet/marshaller.h>

// TODO: marshal/unmarshal messages into the stream for performance purposes.

bool fproto_req_send(fnet_client_t *client, fproto_msg_t msg, void const *data)
{
    switch(msg)
    {
        case FPROTO_HELLO:
        {
            uint32_t const cmd = msg;
            fproto_hello_req_t const *req = (fproto_hello_req_t const *)data;
            if (!fmarshal_u32(client, cmd)) break;
            if (!fmarshal_u32(client, req->version)) break;
            if (!fmarshal_uuid(client, &req->uuid)) break;
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
            if (!funmarshal_u32(client, &req->version)) break;
            if (!funmarshal_uuid(client, &req->uuid)) break;
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
            if (!fmarshal_u32(client, cmd)) break;
            if (!fmarshal_u32(client, res->version)) break;
            if (!fmarshal_uuid(client, &res->uuid)) break;
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
            if (!funmarshal_u32(client, &res->version)) break;
            if (!funmarshal_uuid(client, &res->uuid)) break;
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
    bool ret;

    // I. Send request
    fproto_hello_req_t const req =
    {
        FPROTO_VERSION,
        *uuid
    };

    if (!fnet_acquire(client)) return false;
    do
    {
        ret = false;
        if (!fproto_req_send(client, FPROTO_HELLO, &req)) break;
        ret = true;
    } while(0);
    fnet_release(client);
    if (!ret) return false;

    // II. Read response
    if (!fnet_acquire(client)) return false;
    do
    {
        ret = false;
        uint32_t msg;
        if (!funmarshal_u32(client, &msg)) break;
        if ((msg & FPROTO_RESPONSE) == 0) break;                    // Not response
        msg &= ~FPROTO_RESPONSE;
        if (msg != FPROTO_HELLO) break;                             // Not response for HELLO message

        fproto_hello_res_t res;
        if (!fproto_res_recv(client, (fproto_msg_t)msg, &res)) break;
        if (res.version != FPROTO_VERSION) break;                   // Unsupported protocol version
        if (memcmp(&res.uuid, uuid, sizeof *uuid) == 0) break;      // Client and server are the same node
        *peer_uuid = res.uuid;
        ret = true;
    } while(0);
    fnet_release(client);
    return ret;
}

bool fproto_client_handshake_response(fnet_client_t *client, fuuid_t const *uuid, fuuid_t *peer_uuid)
{
    bool ret;

    // I. Read request
    if (!fnet_acquire(client)) return false;
    do
    {
        ret = false;
        uint32_t msg;
        if (!funmarshal_u32(client, &msg)) break;
        if (msg != FPROTO_HELLO) break;                             // Not HELLO message

        fproto_hello_req_t req;
        if (!fproto_req_recv(client, (fproto_msg_t)msg, &req)) break;
        if (req.version != FPROTO_VERSION) break;                   // Unsupported protocol version
        if (memcmp(&req.uuid, uuid, sizeof *uuid) == 0) break;      // Client and server are the same node
        *peer_uuid = req.uuid;
        ret = true;
    } while(0);
    fnet_release(client);
    if (!ret) return false;

    // II. Send response
    fproto_hello_req_t const res =
    {
        FPROTO_VERSION,
        *uuid
    };

    if (!fnet_acquire(client)) return false;
    do
    {
        ret = false;
        if (!fproto_res_send(client, FPROTO_HELLO, &res)) break;
        ret = true;
    } while(0);
    fnet_release(client);

    return ret;
}

bool fproto_read_message(fnet_client_t *client, fproto_msg_handlers_t *handlers)
{
    bool ret;
    if (!fnet_acquire(client)) return false;
    do
    {
        ret = false;
        uint32_t msg;
        if (!funmarshal_u32(client, &msg)) break;
        // TODO
        ret = true;
    } while(0);
    fnet_release(client);
    return ret;
}
