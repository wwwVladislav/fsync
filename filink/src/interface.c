#include "interface.h"
#include "protocol.h"
#include <fnet/transport.h>
#include <futils/log.h>
#include <futils/mutex.h>
#include <futils/utils.h>
#include <fcommon/messages.h>
#include <fdb/sync/nodes.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stddef.h>
#include <winsock2.h>

typedef struct
{
    fnet_client_t *transport;
    fuuid_t        uuid;
} node_t;

struct filink
{
    volatile uint32_t     ref_counter;
    volatile bool         is_active;
    fuuid_t               uuid;
    pthread_t             thread;
    fnet_server_t        *server;
    unsigned short        port;
    pthread_mutex_t       nodes_mutex;
    volatile size_t       nodes_num;
    node_t                nodes[FMAX_CONNECTIONS_NUM];
    fnet_wait_handler_t   wait_handler;
    fproto_msg_handlers_t proto_handlers;
    fmsgbus_t            *msgbus;
    fdb_t                *db;
};

static uint32_t filink_status_to_proto(uint32_t status)
{
    uint32_t ret = 0;
    if (status & FSTATUS_R4S_DIRS)    ret |= FPROTO_STATUS_READY4SYNC;
    return ret;
}

static uint32_t filink_status_from_proto(uint32_t status)
{
    uint32_t ret = 0;
    if (status & FPROTO_STATUS_READY4SYNC)    ret |= FSTATUS_R4S_DIRS;
    return ret;
}

static bool filink_add_node2db(filink_t *ilink, fnet_client_t *pclient, fuuid_t const *uuid, char const *addr)
{
    fdb_node_info_t node_info;
    strncpy(node_info.address, addr, sizeof node_info.address);

    fdb_transaction_t transaction = { 0 };
    if (fdb_transaction_start(ilink->db, &transaction))
    {
        fdb_nodes_t *nodes = fdb_nodes(&transaction);
        if (nodes)
        {
            fdb_node_add(nodes, &transaction, uuid, &node_info);
            fdb_transaction_commit(&transaction);
            fdb_nodes_release(nodes);
            return true;
        }
        fdb_transaction_abort(&transaction);
    }

    return false;
}

static bool filink_add_node(filink_t *ilink, fnet_client_t *pclient, fuuid_t const *uuid, char const *addr)
{
    if (!filink_add_node2db(ilink, pclient, uuid, addr))
        return false;

    bool ret = true;

    fpush_lock(ilink->nodes_mutex);
    if (ilink->nodes_num < FMAX_CONNECTIONS_NUM)
    {
        for(size_t i = 0; i < ilink->nodes_num; ++i)
        {
            if(ilink->nodes[i].transport
               && memcmp(&ilink->nodes[i].uuid, uuid, sizeof *uuid) == 0)
            {
                ret = false;    // Connection is already exist
                break;
            }
        }

        if (ret)
        {
            ilink->nodes[ilink->nodes_num].uuid = *uuid;
            ilink->nodes[ilink->nodes_num].transport = pclient;
            ilink->nodes_num++;
            fnet_wait_cancel(ilink->wait_handler);
            ret = true;
        }
    }
    else
    {
        ret = false;
        FS_ERR("The maximum allowed connections number is reached");
    }
    fpop_lock();
    return ret;
}

static void filink_remove_node(filink_t *ilink, fnet_client_t *pclient)
{
    fpush_lock(ilink->nodes_mutex);
    size_t i = 0;
    for(; i < ilink->nodes_num; ++i)
    {
        if (ilink->nodes[i].transport == pclient)
        {
            fnet_disconnect(pclient);
            break;
        }
    }

    for(i++; i < ilink->nodes_num; ++i)
        ilink->nodes[i - 1] = ilink->nodes[i];
    ilink->nodes_num--;
    ilink->nodes[ilink->nodes_num].transport = 0;

    fpop_lock();
}

static void filink_broadcast_message(filink_t *ilink, fproto_msg_t msg, void const *data)
{
    fpush_lock(ilink->nodes_mutex);
    for(size_t i = 0; i < ilink->nodes_num; ++i)
    {
        if (ilink->nodes[i].transport && !fproto_send(ilink->nodes[i].transport, msg, (uint8_t const *)data))
            FS_ERR("The broadcast message wasn't send");
    }
    fpop_lock();
}

static void filink_send_message(filink_t *ilink, fuuid_t const *destination, fproto_msg_t msg, void const *data)
{
    fpush_lock(ilink->nodes_mutex);
    for(size_t i = 0; i < ilink->nodes_num; ++i)
    {
        if (ilink->nodes[i].transport && memcmp(&ilink->nodes[i].uuid, destination, sizeof *destination) == 0)
        {
            if (!fproto_send(ilink->nodes[i].transport, msg, (uint8_t const *)data))
                FS_ERR("The message wasn't send");
            break;
        }
    }
    fpop_lock();
}

static void filink_status_handler(filink_t *ilink, FMSG_TYPE(node_status) const *msg)
{
    if (memcmp(&msg->hdr.src, &ilink->uuid, sizeof ilink->uuid) == 0
        && memcmp(&msg->hdr.dst, &ilink->uuid, sizeof ilink->uuid) != 0)
    {
        fproto_node_status_t const pmsg = { msg->hdr.src,  filink_status_to_proto(msg->status) };
        filink_broadcast_message(ilink, FPROTO_NODE_STATUS, &pmsg);
    }
}

static void filink_sync_files_list_handler(filink_t *ilink, FMSG_TYPE(sync_files_list) const *msg)
{
    if (memcmp(&msg->hdr.src, &ilink->uuid, sizeof ilink->uuid) == 0
        && memcmp(&msg->hdr.dst, &ilink->uuid, sizeof ilink->uuid) != 0)
    {
        fproto_sync_files_list_t pmsg =
        {
            msg->hdr.src,
            msg->is_last,
            msg->files_num
        };

        for(uint32_t i = 0; i < msg->files_num; ++i)
        {
            pmsg.files[i].id       = msg->files[i].id;
            pmsg.files[i].digest   = msg->files[i].digest;
            pmsg.files[i].size     = msg->files[i].size;
            pmsg.files[i].is_exist = msg->files[i].is_exist;
            memcpy(pmsg.files[i].path, msg->files[i].path, sizeof msg->files[i].path);
        }

        filink_send_message(ilink, &msg->hdr.dst, FPROTO_SYNC_FILES_LIST, &pmsg);
    }
}

static void filink_file_part_request_handler(filink_t *ilink, FMSG_TYPE(file_part_request) const *msg)
{
    if (memcmp(&msg->hdr.src, &ilink->uuid, sizeof ilink->uuid) == 0
        && memcmp(&msg->hdr.dst, &ilink->uuid, sizeof ilink->uuid) != 0)
    {
        fproto_file_part_request_t pmsg =
        {
            msg->hdr.src,
            msg->id,
            msg->block_number
        };

        filink_send_message(ilink, &msg->hdr.dst, FPROTO_FILE_PART_REQUEST, &pmsg);
    }
}

static void filink_file_part_handler(filink_t *ilink, FMSG_TYPE(file_part) const *msg)
{
    if (memcmp(&msg->hdr.src, &ilink->uuid, sizeof ilink->uuid) == 0
        && memcmp(&msg->hdr.dst, &ilink->uuid, sizeof ilink->uuid) != 0)
    {
        fproto_file_part_t pmsg;
        pmsg.uuid         = msg->hdr.src;
        pmsg.id           = msg->id;
        pmsg.block_number = msg->block_number;
        pmsg.size         = msg->size;
        memcpy(pmsg.data, msg->data, msg->size);

        filink_send_message(ilink, &msg->hdr.dst, FPROTO_FILE_PART, &pmsg);
    }
}

static void filink_msgbus_retain(filink_t *ilink, fmsgbus_t *pmsgbus)
{
    ilink->msgbus = fmsgbus_retain(pmsgbus);
    fmsgbus_subscribe(ilink->msgbus, FNODE_STATUS,          (fmsg_handler_t)filink_status_handler,            ilink);
    fmsgbus_subscribe(ilink->msgbus, FSYNC_FILES_LIST,      (fmsg_handler_t)filink_sync_files_list_handler,   ilink);
    fmsgbus_subscribe(ilink->msgbus, FFILE_PART_REQUEST,    (fmsg_handler_t)filink_file_part_request_handler, ilink);
    fmsgbus_subscribe(ilink->msgbus, FFILE_PART,            (fmsg_handler_t)filink_file_part_handler,         ilink);
}

static void filink_msgbus_release(filink_t *ilink)
{
    fmsgbus_unsubscribe(ilink->msgbus, FNODE_STATUS,        (fmsg_handler_t)filink_status_handler);
    fmsgbus_unsubscribe(ilink->msgbus, FSYNC_FILES_LIST,    (fmsg_handler_t)filink_sync_files_list_handler);
    fmsgbus_unsubscribe(ilink->msgbus, FFILE_PART_REQUEST,  (fmsg_handler_t)filink_file_part_request_handler);
    fmsgbus_unsubscribe(ilink->msgbus, FFILE_PART,          (fmsg_handler_t)filink_file_part_handler);
    fmsgbus_release(ilink->msgbus);
}

static void fproto_node_status_handler(filink_t *ilink, fproto_node_status_t const *pmsg)
{
    FMSG(node_status, msg, pmsg->uuid, ilink->uuid,
        filink_status_from_proto(pmsg->status)
    );
    fmsgbus_publish(ilink->msgbus, FNODE_STATUS, (fmsg_t const *)&msg);
}

static void fproto_sync_files_list_handler(filink_t *ilink, fproto_sync_files_list_t const *pmsg)
{
    FMSG(sync_files_list, msg, pmsg->uuid, ilink->uuid,
        pmsg->is_last,
        pmsg->files_num
    );

    for(uint32_t i = 0; i < pmsg->files_num; ++i)
    {
        msg.files[i].id       = pmsg->files[i].id;
        msg.files[i].digest   = pmsg->files[i].digest;
        msg.files[i].size     = pmsg->files[i].size;
        msg.files[i].is_exist = pmsg->files[i].is_exist;
        memcpy(msg.files[i].path, pmsg->files[i].path, sizeof pmsg->files[i].path);
    }

    fmsgbus_publish(ilink->msgbus, FSYNC_FILES_LIST, (fmsg_t const *)&msg);
}

static void fproto_file_part_request_handler(filink_t *ilink, fproto_file_part_request_t const *pmsg)
{
    FMSG(file_part_request, msg, pmsg->uuid, ilink->uuid,
        pmsg->id,
        pmsg->block_number
    );

    fmsgbus_publish(ilink->msgbus, FFILE_PART_REQUEST, (fmsg_t const *)&msg);
}

static void fproto_file_part_handler(filink_t *ilink, fproto_file_part_t const *pmsg)
{
    FMSG(file_part, msg, pmsg->uuid, ilink->uuid,
        pmsg->id,
        pmsg->block_number,
        pmsg->size
    );
    memcpy(msg.data, pmsg->data, msg.size);
    fmsgbus_publish(ilink->msgbus, FFILE_PART, (fmsg_t const *)&msg);
}

static void filink_clients_accepter(fnet_server_t const *pserver, fnet_client_t *pclient)
{
    filink_t *ilink = (filink_t *)fnet_server_get_param(pserver);

    if (pclient)
    {
        unsigned short listen_port = 0;
        unsigned short peer_port = 0;
        fuuid_t peer_uuid;

        if (fnet_server_get_port(pserver, &listen_port)
            && fproto_client_handshake_response(pclient, listen_port, &ilink->uuid, &peer_port, &peer_uuid))
        {
            fnet_address_t addr = *fnet_peer_address(pclient);
            ((struct sockaddr_in *)&addr)->sin_port = htons(peer_port);
            char str_addr[256];

            if (!fnet_addr2str(&addr, str_addr, sizeof str_addr)
                || !filink_add_node(ilink, pclient, &peer_uuid, str_addr))
                fnet_disconnect(pclient);
            else
                FS_INFO("New connection accepted. UUID: %llx%llx", peer_uuid.data.u64[0], peer_uuid.data.u64[1]);
        }
        else
        {
            fnet_disconnect(pclient);
            FS_ERR("New connection acception is failed");
        }
    }
}

static void *filink_thread(void *param)
{
    filink_t *ilink = (filink_t*)param;
    ilink->is_active = true;

    size_t         clients_num = 0;
    size_t         rclients_num = 0;
    size_t         eclients_num = 0;
    fnet_client_t *clients[FMAX_CONNECTIONS_NUM];
    fnet_client_t *rclients[FMAX_CONNECTIONS_NUM];
    fnet_client_t *eclients[FMAX_CONNECTIONS_NUM];

    while(ilink->is_active)
    {
        fpush_lock(ilink->nodes_mutex);
        ilink->wait_handler = fnet_wait_handler();
        for(size_t i = 0; i < ilink->nodes_num; ++i)
            clients[i] = ilink->nodes[i].transport;
        clients_num = ilink->nodes_num;
        fpop_lock();

        bool ret = fnet_select(clients,
                               clients_num,
                               rclients,
                               &rclients_num,
                               eclients,
                               &eclients_num,
                               ilink->wait_handler);
        if (!ret) continue;

        for(size_t i = 0; i < rclients_num; ++i)
        {
            if (!fproto_read_message(rclients[i], &ilink->proto_handlers))
            {
                FS_INFO("Node has disconnected");
                filink_remove_node(ilink, rclients[i]);
            }
        }

        for(size_t i = 0; i < eclients_num; ++i)
            filink_remove_node(ilink, eclients[i]);
    }

    return 0;
}

filink_t *filink_bind(fmsgbus_t *pmsgbus, fdb_t *db, char const *addr, fuuid_t const *uuid)
{
    if (!pmsgbus)
    {
        FS_ERR("Invalid messages bus");
        return 0;
    }

    if (!db)
    {
        FS_ERR("Invalid DB");
        return 0;
    }

    if (!addr)
    {
        FS_ERR("Invalid address");
        return 0;
    }

    if (!uuid)
    {
        FS_ERR("Invalid UUID");
        return 0;
    }

    filink_t *ilink = malloc(sizeof(filink_t));
    if (!ilink)
    {
        FS_ERR("Unable to allocate memory for nodes interlink");
        return 0;
    }
    memset(ilink, 0, sizeof *ilink);

    ilink->ref_counter = 1;
    ilink->uuid = *uuid;
    ilink->db = fdb_retain(db);

    filink_msgbus_retain(ilink, pmsgbus);

    ilink->proto_handlers.param = ilink;
    ilink->proto_handlers.node_status_handler       = (fproto_node_status_handler_t)fproto_node_status_handler;
    ilink->proto_handlers.sync_files_list_handler   = (fproto_sync_files_list_handler_t)fproto_sync_files_list_handler;
    ilink->proto_handlers.file_part_request_handler = (fproto_file_part_request_handler_t)fproto_file_part_request_handler;
    ilink->proto_handlers.file_part_handler         = (fproto_file_part_handler_t)fproto_file_part_handler;

    ilink->server = fnet_bind(FNET_SSL, addr, filink_clients_accepter);
    if (!ilink->server)
    {
        FS_ERR("Unable to bind the interlink");
        filink_release(ilink);
        return 0;
    }

    if (!fnet_server_get_port(ilink->server, &ilink->port))
    {
        filink_release(ilink);
        return 0;
    }

    fnet_server_set_param(ilink->server, ilink);

    ilink->nodes_num = 0;

    ilink->nodes_mutex = PTHREAD_MUTEX_INITIALIZER;

    int rc = pthread_create(&ilink->thread, 0, filink_thread, (void*)ilink);
    if (rc)
    {
        FS_ERR("Unable to create the thread for clients processing. Error: %d", rc);
        filink_release(ilink);
        return 0;
    }

    static struct timespec const ts = { 1, 0 };
    while(!ilink->is_active)
        nanosleep(&ts, NULL);

    return ilink;
}

void filink_unbind(filink_t *ilink)
{
    if (ilink)
    {
        if (ilink->is_active)
        {
            ilink->is_active = false;
            fnet_wait_cancel(ilink->wait_handler);
            pthread_join(ilink->thread, 0);
        }

        fnet_unbind(ilink->server);
        ilink->server = 0;
    }
}

static void filink_disconnect_all(filink_t *ilink)
{
    for(int i = 0; i < FARRAY_SIZE(ilink->nodes); ++i)
    {
        if (ilink->nodes[i].transport)
            fnet_disconnect(ilink->nodes[i].transport);
    }
}

filink_t *filink_retain(filink_t *ilink)
{
    if (ilink)
        ilink->ref_counter++;
    else
        FS_ERR("Invalid interlink");
    return ilink;
}

void filink_release(filink_t *ilink)
{
    if (ilink)
    {
        if (!ilink->ref_counter)
            FS_ERR("Invalid interlink");
        else if (!--ilink->ref_counter)
        {
            filink_unbind(ilink);
            filink_disconnect_all(ilink);
            filink_msgbus_release(ilink);
            fdb_release(ilink->db);
            memset(ilink, 0, sizeof *ilink);
            free(ilink);
        }
    }
    else
        FS_ERR("Invalid interlink");
}

bool filink_connect(filink_t *ilink, char const *addr)
{
    if (!ilink)
    {
        FS_ERR("Invalid interlink");
        return false;
    }

    if (!addr)
    {
        FS_ERR("Invalid argument");
        return false;
    }

    if (ilink->nodes_num >= FMAX_CONNECTIONS_NUM)
    {
        FS_ERR("The maximum allowed connections number is reached: %u", ilink->nodes_num);
        return false;
    }

    fnet_client_t *pclient = fnet_connect(FNET_SSL, addr);
    if (!pclient) return false;

    fuuid_t peer_uuid = { 0 };
    uint16_t peer_listen_port = 0;

    if (!fproto_client_handshake_request(pclient, ilink->port, &ilink->uuid, &peer_listen_port, &peer_uuid))
    {
        fnet_disconnect(pclient);
        return false;
    }

    return filink_add_node(ilink, pclient, &peer_uuid, addr);
}

bool filink_is_connected(filink_t *ilink, fuuid_t const *uuid)
{
    if (!ilink || !uuid)
        return false;

    for(int i = 0; i < FARRAY_SIZE(ilink->nodes); ++i)
    {
        if (ilink->nodes[i].transport &&
            memcmp(&ilink->nodes[i].uuid, uuid, sizeof(*uuid)) == 0)
            return true;
    }

    return false;
}
