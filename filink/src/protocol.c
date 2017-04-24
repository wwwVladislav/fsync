#include "protocol.h"
#include <futils/log.h>
#include <fnet/marshaller.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>

typedef enum
{
    FPROTO_FIELD_NULL = 0,
    FPROTO_FIELD_UINT8,
    FPROTO_FIELD_UINT16,
    FPROTO_FIELD_UINT32,
    FPROTO_FIELD_UINT64,
    FPROTO_FIELD_BOOL,
    FPROTO_FIELD_UUID,
    FPROTO_FIELD_STRING,
    FPROTO_FIELD_STRUCT
} fproto_field_t;

typedef struct fproto_field_desc
{
    fproto_field_t            type;
    size_t                    size;
    size_t                    offset;
    int                       number;
    struct fproto_field_desc const *struct_desc;
} fproto_field_desc_t;

#define FPROTO_DESC_TABLE_NAME(msg_type) fproto_##msg_type##_desc
#define FPROTO_DESC_TABLE(msg_type) static fproto_field_desc_t const FPROTO_DESC_TABLE_NAME(msg_type)[] =

FPROTO_DESC_TABLE(FPROTO_HELLO)
{
    { FPROTO_FIELD_UUID,   sizeof(fuuid_t),  offsetof(fproto_hello_t, uuid),        1 },
    { FPROTO_FIELD_UINT32, sizeof(uint32_t), offsetof(fproto_hello_t, version),     1 },
    { FPROTO_FIELD_UINT16, sizeof(uint16_t), offsetof(fproto_hello_t, listen_port), 1 },
    { FPROTO_FIELD_NULL,   0,                0,                                     0 }
};

FPROTO_DESC_TABLE(FPROTO_NODE_STATUS)
{
    { FPROTO_FIELD_UUID,   sizeof(fuuid_t),  offsetof(fproto_node_status_t, uuid),   1 },
    { FPROTO_FIELD_UINT32, sizeof(uint32_t), offsetof(fproto_node_status_t, status), 1 },
    { FPROTO_FIELD_NULL,   0,                0,                                      0 }
};

FPROTO_DESC_TABLE(FPROTO_SYNC_FILE_INFO)
{
    { FPROTO_FIELD_UINT32, sizeof(uint32_t), offsetof(fproto_sync_file_info_t, id),       1 },
    { FPROTO_FIELD_STRING, sizeof(char),     offsetof(fproto_sync_file_info_t, path),     FPROTO_MAX_PATH },
    { FPROTO_FIELD_UINT8,  sizeof(uint8_t),  offsetof(fproto_sync_file_info_t, digest),   sizeof(fmd5_t) },
    { FPROTO_FIELD_UINT64, sizeof(uint64_t), offsetof(fproto_sync_file_info_t, size),     1 },
    { FPROTO_FIELD_BOOL,   sizeof(bool),     offsetof(fproto_sync_file_info_t, is_exist), 1 },
    { FPROTO_FIELD_NULL,   0,                0,                                           0 }
};

FPROTO_DESC_TABLE(FPROTO_SYNC_FILES_LIST)
{
    { FPROTO_FIELD_UUID,   sizeof(fuuid_t),                 offsetof(fproto_sync_files_list_t, uuid),      1 },
    { FPROTO_FIELD_BOOL,   sizeof(bool),                    offsetof(fproto_sync_files_list_t, is_last),   1 },
    { FPROTO_FIELD_UINT8,  sizeof(uint8_t),                 offsetof(fproto_sync_files_list_t, files_num), 1 },
    { FPROTO_FIELD_STRUCT, sizeof(fproto_sync_file_info_t), offsetof(fproto_sync_files_list_t, files),     -2, FPROTO_DESC_TABLE_NAME(FPROTO_SYNC_FILE_INFO) },
    { FPROTO_FIELD_NULL,   0,                               0,                                             0 }
};

FPROTO_DESC_TABLE(FPROTO_FILE_PATH)
{
    { FPROTO_FIELD_STRING, sizeof(char), 0, FPROTO_MAX_PATH },
    { FPROTO_FIELD_NULL,   0,            0, 0 }
};

FPROTO_DESC_TABLE(FPROTO_FILE_PART_REQUEST)
{
    { FPROTO_FIELD_UUID,   sizeof(fuuid_t),  offsetof(fproto_file_part_request_t, uuid),         1 },
    { FPROTO_FIELD_UINT32, sizeof(uint32_t), offsetof(fproto_file_part_request_t, id),           1 },
    { FPROTO_FIELD_UINT32, sizeof(uint32_t), offsetof(fproto_file_part_request_t, block_number), 1 },
    { FPROTO_FIELD_NULL,   0,                0,                                                  0 }
};

FPROTO_DESC_TABLE(FPROTO_FILE_PART)
{
    { FPROTO_FIELD_UUID,   sizeof(fuuid_t),  offsetof(fproto_file_part_t, uuid),         1 },
    { FPROTO_FIELD_UINT32, sizeof(uint32_t), offsetof(fproto_file_part_t, id),           1 },
    { FPROTO_FIELD_UINT32, sizeof(uint32_t), offsetof(fproto_file_part_t, block_number), 1 },
    { FPROTO_FIELD_UINT16, sizeof(uint16_t), offsetof(fproto_file_part_t, size),         1 },
    { FPROTO_FIELD_UINT8,  sizeof(uint8_t),  offsetof(fproto_file_part_t, data),         -3 },
    { FPROTO_FIELD_NULL,   0,                0,                                          0 }
};

static fproto_field_desc_t const* fproto_messages[] =
{
    FPROTO_DESC_TABLE_NAME(FPROTO_HELLO),
    FPROTO_DESC_TABLE_NAME(FPROTO_NODE_STATUS),
    FPROTO_DESC_TABLE_NAME(FPROTO_SYNC_FILES_LIST),
    FPROTO_DESC_TABLE_NAME(FPROTO_FILE_PART_REQUEST),
    FPROTO_DESC_TABLE_NAME(FPROTO_FILE_PART)
};

static uint8_t const *fproto_marshal_struct(fnet_client_t *, fproto_field_desc_t const*, uint8_t const *);
static uint8_t *fproto_unmarshal_struct(fnet_client_t *, fproto_field_desc_t const*, uint8_t *);

static uint8_t const *fproto_marshal(fnet_client_t *client, fproto_field_t type, size_t size, uint32_t number, fproto_field_desc_t const* desc, void const *ptr)
{
    switch(type)
    {
        case FPROTO_FIELD_UINT8:
        {
            uint8_t const *arr = (uint8_t const *)ptr;
            if (!fmarshal(client, arr, number))
                return 0;
            return arr + number;
        }

        case FPROTO_FIELD_UINT16:
        {
            uint16_t const *arr = (uint16_t const *)ptr;
            for(uint32_t i = 0; i < number; ++i)
            {
                if (!fmarshal_u16(client, arr[i]))
                    return 0;
            }
            return (uint8_t const *)(arr + number);
        }

        case FPROTO_FIELD_UINT32:
        {
            uint32_t const *arr = (uint32_t const *)ptr;
            for(uint32_t i = 0; i < number; ++i)
            {
                if (!fmarshal_u32(client, arr[i]))
                    return 0;
            }
            return (uint8_t const *)(arr + number);
        }

        case FPROTO_FIELD_UINT64:
        {
            uint64_t const *arr = (uint64_t const *)ptr;
            for(uint32_t i = 0; i < number; ++i)
            {
                if (!fmarshal_u64(client, arr[i]))
                    return 0;
            }
            return (uint8_t const *)(arr + number);
        }

        case FPROTO_FIELD_BOOL:
        {
            bool const *arr = (bool const *)ptr;
            for(uint32_t i = 0; i < number; ++i)
            {
                if (!fmarshal_bool(client, arr[i]))
                    return 0;
            }
            return (uint8_t const *)(arr + number);
        }

        case FPROTO_FIELD_UUID:
        {
            fuuid_t const *arr = (fuuid_t const *)ptr;
            for(uint32_t i = 0; i < number; ++i)
            {
                if (!fmarshal_uuid(client, &arr[i]))
                    return 0;
            }
            return (uint8_t const *)(arr + number);
        }

        case FPROTO_FIELD_STRING:
        {
            char const *str = (char const *)ptr;
            uint32_t const len = strlen(str);
            if (!fmarshal_u32(client, len))
                return 0;
            if (!fmarshal(client, (uint8_t const *)str, len))
                return 0;
            return (uint8_t const *)(str + number);
        }

        case FPROTO_FIELD_STRUCT:
        {
            uint8_t const *next = ptr;
            for(uint32_t i = 0; i < number; ++i)
            {
                next = fproto_marshal_struct(client, desc, next);
                if (!next)
                    break;
                next = (uint8_t const *)ptr + size * (i + 1);
            }
            return next;
        }

        default:
            break;
    }
    return 0;
}

static uint8_t const *fproto_marshal_struct(fnet_client_t *client, fproto_field_desc_t const* desc, uint8_t const *ptr)
{
    uint8_t const *next = 0;
    fproto_field_desc_t const *field = desc;

    for(; field->type != FPROTO_FIELD_NULL; ++field)
    {
        uint32_t number = 1;
        if (field->number > 0)
            number = field->number;
        else
        {
            fproto_field_desc_t const* size_desc = desc + abs(field->number);
            if (size_desc->type == FPROTO_FIELD_UINT8)
                number = *(uint8_t*)(ptr + size_desc->offset);
            else if (size_desc->type == FPROTO_FIELD_UINT16)
                number = *(uint16_t*)(ptr + size_desc->offset);
            else if (size_desc->type == FPROTO_FIELD_UINT32)
                number = *(uint32_t*)(ptr + size_desc->offset);
            else
            {
                FS_ERR("Invalid size reference");
                break;
            }
        }
        next = fproto_marshal(client, field->type, field->size, number, field->struct_desc, ptr + field->offset);
        if (!next)
            return 0;
    }

    return next;
}

static uint8_t *fproto_unmarshal(fnet_client_t *client, fproto_field_t type, size_t size, uint32_t number, fproto_field_desc_t const* desc, void *ptr)
{
    switch(type)
    {
        case FPROTO_FIELD_UINT8:
        {
            uint8_t *arr = (uint8_t *)ptr;
            if (!funmarshal(client, arr, number))
                return 0;
            return arr + number;
        }

        case FPROTO_FIELD_UINT16:
        {
            uint16_t *arr = (uint16_t *)ptr;
            for(uint32_t i = 0; i < number; ++i)
            {
                if (!funmarshal_u16(client, arr + i))
                    return 0;
            }
            return (uint8_t *)(arr + number);
        }

        case FPROTO_FIELD_UINT32:
        {
            uint32_t *arr = (uint32_t *)ptr;
            for(uint32_t i = 0; i < number; ++i)
            {
                if (!funmarshal_u32(client, arr + i))
                    return 0;
            }
            return (uint8_t *)(arr + number);
        }

        case FPROTO_FIELD_UINT64:
        {
            uint64_t *arr = (uint64_t *)ptr;
            for(uint32_t i = 0; i < number; ++i)
            {
                if (!funmarshal_u64(client, arr + i))
                    return 0;
            }
            return (uint8_t *)(arr + number);
        }

        case FPROTO_FIELD_BOOL:
        {
            bool *arr = (bool *)ptr;
            for(uint32_t i = 0; i < number; ++i)
            {
                if (!funmarshal_bool(client, arr + i))
                    return 0;
            }
            return (uint8_t *)(arr + number);
        }

        case FPROTO_FIELD_UUID:
        {
            fuuid_t *arr = (fuuid_t *)ptr;
            for(uint32_t i = 0; i < number; ++i)
            {
                if (!funmarshal_uuid(client, arr + i))
                    return 0;
            }
            return (uint8_t *)(arr + number);
        }

        case FPROTO_FIELD_STRING:
        {
            char *str = (char *)ptr;
            uint32_t len = 0;
            if (!funmarshal_u32(client, &len))
                return 0;
            if (!funmarshal(client, (uint8_t*)str, len))
                return 0;
            if (len < number)
                str[len] = 0;
            return (uint8_t *)(str + number);
        }

        case FPROTO_FIELD_STRUCT:
        {
            uint8_t *next = ptr;
            for(uint32_t i = 0; i < number; ++i)
            {
                next = fproto_unmarshal_struct(client, desc, next);
                if (!next)
                    break;
                next = (uint8_t *)ptr + size * (i + 1);
            }
            return next;
        }

        default:
            break;
    }
    return 0;
}

static uint8_t *fproto_unmarshal_struct(fnet_client_t *client, fproto_field_desc_t const* desc, uint8_t *ptr)
{
    uint8_t *next = 0;
    fproto_field_desc_t const *field = desc;

    for(; field->type != FPROTO_FIELD_NULL; ++field)
    {
        uint32_t number = 1;
        if (field->number > 0)
            number = field->number;
        else
        {
            fproto_field_desc_t const* size_desc = desc + abs(field->number);
            if (size_desc->type == FPROTO_FIELD_UINT8)
                number = *(uint8_t*)(ptr + size_desc->offset);
            else if (size_desc->type == FPROTO_FIELD_UINT16)
                number = *(uint16_t*)(ptr + size_desc->offset);
            else if (size_desc->type == FPROTO_FIELD_UINT32)
                number = *(uint32_t*)(ptr + size_desc->offset);
            else
            {
                FS_ERR("Invalid size reference");
                break;
            }
        }
        next = fproto_unmarshal(client, field->type, field->size, number, field->struct_desc, ptr + field->offset);
        if (!next)
            return 0;
    }

    return next;
}

// TODO: marshal/unmarshal messages into the stream for performance purposes.
bool fproto_send(fnet_client_t *client, fproto_msg_t msg, uint8_t const *data)
{
    if (msg < 0 || msg > sizeof fproto_messages / sizeof *fproto_messages)
    {
        FS_ERR("Unknown message type");
        return false;
    }

    bool ret;
    uint32_t const cmd = msg;

    if (!fnet_acquire(client))
        return false;

    do
    {
        ret = false;

        if (!fmarshal_u32(client, cmd))
            break;

        ret = fproto_marshal_struct(client, fproto_messages[msg], data) != 0;
    } while(0);

    fnet_release(client);

    return ret;
}

static bool fproto_recv(fnet_client_t *client, fproto_msg_t msg, uint8_t *data)
{
    if (msg < 0 || msg > sizeof fproto_messages / sizeof *fproto_messages)
    {
        FS_ERR("Unknown message type");
        return false;
    }

    return fproto_unmarshal_struct(client, fproto_messages[msg], data) != 0;
}

bool fproto_client_handshake_request(fnet_client_t *client, uint16_t listen_port, fuuid_t const *uuid, uint16_t *peer_listen_port, fuuid_t *peer_uuid)
{
    bool ret;

    // I. Send request
    fproto_hello_t const req =
    {
        *uuid,
        FPROTO_VERSION,
        listen_port
    };

    do
    {
        ret = false;
        if (!fproto_send(client, FPROTO_HELLO, (uint8_t const *)&req)) break;
        ret = true;
    } while(0);
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
        *peer_listen_port = res.listen_port;
        *peer_uuid = res.uuid;
        ret = true;
    } while(0);
    fnet_release(client);
    return ret;
}

bool fproto_client_handshake_response(fnet_client_t *client, uint16_t listen_port, fuuid_t const *uuid, uint16_t *peer_listen_port, fuuid_t *peer_uuid)
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
        *peer_listen_port = req.listen_port;
        *peer_uuid = req.uuid;
        ret = true;
    } while(0);
    fnet_release(client);
    if (!ret) return false;

    // II. Send response
    fproto_hello_t const res =
    {
        *uuid,
        FPROTO_VERSION,
        listen_port
    };

    do
    {
        ret = false;
        if (!fproto_send(client, FPROTO_HELLO, (uint8_t const *)&res)) break;
        ret = true;
    } while(0);

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
                    handlers->node_status_handler(handlers->param, &req);
                ret = true;
                break;
            }

            case FPROTO_SYNC_FILES_LIST:
            {
                fproto_sync_files_list_t req;
                if (!fproto_recv(client, (fproto_msg_t)msg, (uint8_t *)&req)) break;
                if (handlers->sync_files_list_handler)
                    handlers->sync_files_list_handler(handlers->param, &req);
                ret = true;
                break;
            }

            case FPROTO_FILE_PART_REQUEST:
            {
                fproto_file_part_request_t req;
                if (!fproto_recv(client, (fproto_msg_t)msg, (uint8_t *)&req)) break;
                if (handlers->file_part_request_handler)
                    handlers->file_part_request_handler(handlers->param, &req);
                ret = true;
                break;
            }

            case FPROTO_FILE_PART:
            {
                fproto_file_part_t req;
                if (!fproto_recv(client, (fproto_msg_t)msg, (uint8_t *)&req)) break;
                if (handlers->file_part_handler)
                    handlers->file_part_handler(handlers->param, &req);
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
