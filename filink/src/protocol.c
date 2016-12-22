#include "protocol.h"
#include <futils/log.h>
#include <string.h>
#include <stddef.h>
#include <fnet/marshaller.h>

typedef enum
{
    FPROTO_FIELD_NULL = 0,
    FPROTO_FIELD_UINT8,
    FPROTO_FIELD_UINT16,
    FPROTO_FIELD_UINT32,
    FPROTO_FIELD_BOOL,
    FPROTO_FIELD_UUID
} fproto_field_t;

typedef struct
{
    fproto_field_t type;
    size_t         offset;
    size_t         size;
} fproto_field_desc_t;

#define FPROTO_DESC_TABLE_NAME(msg_type) fproto_##msg_type##_desc
#define FPROTO_DESC_TABLE(msg_type) static fproto_field_desc_t const FPROTO_DESC_TABLE_NAME(msg_type)[] =

FPROTO_DESC_TABLE(FPROTO_HELLO)
{
    { FPROTO_FIELD_UUID,   offsetof(fproto_hello_t, uuid),          sizeof(fuuid_t) },
    { FPROTO_FIELD_UINT32, offsetof(fproto_hello_t, version),       sizeof(uint32_t) },
    { FPROTO_FIELD_NULL,   0,                                       0 }
};

FPROTO_DESC_TABLE(FPROTO_NODE_STATUS)
{
    { FPROTO_FIELD_UUID,   offsetof(fproto_node_status_t, uuid),    sizeof(fuuid_t) },
    { FPROTO_FIELD_UINT32, offsetof(fproto_node_status_t, status),  sizeof(uint32_t) },
    { FPROTO_FIELD_NULL,   0,                                       0 }
};

static fproto_field_desc_t const* fproto_messages[] =
{
    FPROTO_DESC_TABLE_NAME(FPROTO_HELLO),
    FPROTO_DESC_TABLE_NAME(FPROTO_NODE_STATUS)
};

static bool fproto_marshal(fnet_client_t *client, fproto_field_desc_t const* field_desc, void const *ptr)
{
    switch(field_desc->type)
    {
        case FPROTO_FIELD_UINT8:    return fmarshal_u8(client,   *(uint8_t const *)ptr);
        case FPROTO_FIELD_UINT16:   return fmarshal_u16(client,  *(uint16_t const *)ptr);
        case FPROTO_FIELD_UINT32:   return fmarshal_u32(client,  *(uint32_t const *)ptr);
        case FPROTO_FIELD_BOOL:     return fmarshal_bool(client, *(bool const *)ptr);
        case FPROTO_FIELD_UUID:     return fmarshal_uuid(client,  (fuuid_t const *)ptr);
        default:
            break;
    }
    return false;
}

static bool fproto_unmarshal(fnet_client_t *client, fproto_field_desc_t const* field_desc, void *ptr)
{
    switch(field_desc->type)
    {
        case FPROTO_FIELD_UINT8:    return funmarshal_u8(client,   (uint8_t *)ptr);
        case FPROTO_FIELD_UINT16:   return funmarshal_u16(client,  (uint16_t *)ptr);
        case FPROTO_FIELD_UINT32:   return funmarshal_u32(client,  (uint32_t *)ptr);
        case FPROTO_FIELD_BOOL:     return funmarshal_bool(client, (bool *)ptr);
        case FPROTO_FIELD_UUID:     return funmarshal_uuid(client, (fuuid_t *)ptr);
        default:
            break;
    }
    return false;
}

// TODO: marshal/unmarshal messages into the stream for performance purposes.
bool fproto_send(fnet_client_t *client, fproto_msg_t msg, uint8_t const *data)
{
    if (msg < 0 || msg > sizeof fproto_messages / sizeof *fproto_messages)
    {
        FS_ERR("Unknown message type");
        return false;
    }

    uint32_t const cmd = msg;
    if (!fmarshal_u32(client, cmd))
        return false;

    for(fproto_field_desc_t const* desc = fproto_messages[msg];
        desc->type != FPROTO_FIELD_NULL;
        ++desc)
    {
        if (!fproto_marshal(client, desc, data + desc->offset))
            return false;
    }

    return true;
}

bool fproto_recv(fnet_client_t *client, fproto_msg_t msg, uint8_t *data)
{
    if (msg < 0 || msg > sizeof fproto_messages / sizeof *fproto_messages)
    {
        FS_ERR("Unknown message type");
        return false;
    }

    for(fproto_field_desc_t const* desc = fproto_messages[msg];
        desc->type != FPROTO_FIELD_NULL;
        ++desc)
    {
        if (!fproto_unmarshal(client, desc, data + desc->offset))
            return false;
    }

    return true;
}

bool fproto_client_handshake_request(fnet_client_t *client, fuuid_t const *uuid, fuuid_t *peer_uuid)
{
    bool ret;

    // I. Send request
    fproto_hello_t const req =
    {
        *uuid,
        FPROTO_VERSION
    };

    if (!fnet_acquire(client)) return false;
    do
    {
        ret = false;
        if (!fproto_send(client, FPROTO_HELLO, (uint8_t const *)&req)) break;
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
        if (msg != FPROTO_HELLO) break;                             // Not HELLO message

        fproto_hello_t res;
        if (!fproto_recv(client, (fproto_msg_t)msg, (uint8_t *)&res)) break;
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

        fproto_hello_t req;
        if (!fproto_recv(client, (fproto_msg_t)msg, (uint8_t *)&req)) break;
        if (req.version != FPROTO_VERSION) break;                   // Unsupported protocol version
        if (memcmp(&req.uuid, uuid, sizeof *uuid) == 0) break;      // Client and server are the same node
        *peer_uuid = req.uuid;
        ret = true;
    } while(0);
    fnet_release(client);
    if (!ret) return false;

    // II. Send response
    fproto_hello_t const res =
    {
        *uuid,
        FPROTO_VERSION
    };

    if (!fnet_acquire(client)) return false;
    do
    {
        ret = false;
        if (!fproto_send(client, FPROTO_HELLO, (uint8_t const *)&res)) break;
        ret = true;
    } while(0);
    fnet_release(client);

    return ret;
}

bool fproto_notify_node_status(fnet_client_t *client, fuuid_t const *uuid, uint32_t status)
{
    fproto_node_status_t const msg =
    {
        *uuid,
        status
    };
    if (!fnet_acquire(client)) return false;
    bool ret = fproto_send(client, FPROTO_NODE_STATUS, (uint8_t const *)&msg);
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

        switch(msg)
        {
            case FPROTO_NODE_STATUS:
            {
                fproto_node_status_t req;
                if (!fproto_recv(client, (fproto_msg_t)msg, (uint8_t *)&req)) break;
                if (handlers->node_status_handler)
                    handlers->node_status_handler(handlers->user_data, &req.uuid, req.status);
                ret = true;
                break;
            }

            default:
            {
                FS_ERR("Unknown message type");
                break;
            }
        }
    } while(0);
    fnet_release(client);
    return ret;
}
