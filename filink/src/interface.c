#include "interface.h"
#include "protocol.h"
#include <fnet/transport.h>
#include <futils/log.h>
#include <messages.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stddef.h>

enum
{
    FILINK_MAX_ALLOWED_CONNECTIONS_NUM = 64
};

typedef struct
{
    fnet_client_t *transport;
    fuuid_t        uuid;
} node_t;

struct filink
{
    volatile bool         is_active;
    fuuid_t               uuid;
    pthread_t             thread;
    fnet_server_t        *server;
    pthread_mutex_t       nodes_mutex;
    volatile size_t       nodes_num;
    node_t                nodes[FILINK_MAX_ALLOWED_CONNECTIONS_NUM];
    fnet_wait_handler_t   wait_handler;
    fproto_msg_handlers_t proto_handlers;
    fmsgbus_t            *msgbus;
};

#define filink_push_lock(mutex)                     \
    if (pthread_mutex_lock(&mutex))	            \
        FS_ERR("The mutex locking is failed");      \
    else                                            \
        pthread_cleanup_push((void (*)())pthread_mutex_unlock, (void *)&mutex);

#define filink_pop_lock() pthread_cleanup_pop(1);

static uint32_t filink_status_to_proto(uint32_t status)
{
    uint32_t ret = 0;
    if (status & FSTATUS_READY4SYNC)    ret |= FPROTO_STATUS_READY4SYNC;
    return ret;
}

static uint32_t filink_status_from_proto(uint32_t status)
{
    uint32_t ret = 0;
    if (status & FPROTO_STATUS_READY4SYNC)    ret |= FSTATUS_READY4SYNC;
    return ret;
}

static bool filink_add_node(filink_t *ilink, fnet_client_t *pclient, fuuid_t const *uuid)
{
    bool ret = true;

    filink_push_lock(ilink->nodes_mutex);
    if (ilink->nodes_num < FILINK_MAX_ALLOWED_CONNECTIONS_NUM)
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
    filink_pop_lock();
    return ret;
}

static void filink_remove_node(filink_t *ilink, fnet_client_t *pclient)
{
    filink_push_lock(ilink->nodes_mutex);
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

    filink_pop_lock();
}

static void filink_broadcast_message(filink_t *ilink, fproto_msg_t msg, void const *data)
{
    uint32_t failed_connections_num = 0;
    fnet_client_t *failed_connections[FILINK_MAX_ALLOWED_CONNECTIONS_NUM] = { 0 };

    filink_push_lock(ilink->nodes_mutex);
    for(size_t i = 0; i < ilink->nodes_num; ++i)
    {
        if (ilink->nodes[i].transport && !fproto_send(ilink->nodes[i].transport, msg, (uint8_t const *)data))
            failed_connections[failed_connections_num++] = ilink->nodes[i].transport;
    }
    filink_pop_lock();

    for (uint32_t i = 0; i < failed_connections_num; ++i)
        filink_remove_node(ilink, failed_connections[i]);
}

static void filink_status_handler(filink_t *ilink, uint32_t msg_type, fmsg_node_status_t const *msg, uint32_t size)
{
    (void)size;
    if (memcmp(&msg->uuid, &ilink->uuid, sizeof ilink->uuid) != 0)
        return;
    fproto_node_status_t const pmsg = { msg->uuid,  filink_status_to_proto(msg->status) };
    filink_broadcast_message(ilink, FPROTO_NODE_STATUS, &pmsg);
}

static void filink_msgbus_retain(filink_t *ilink, fmsgbus_t *pmsgbus)
{
    ilink->msgbus = fmsgbus_retain(pmsgbus);
    fmsgbus_subscribe(ilink->msgbus, FNODE_STATUS, (fmsg_handler_t)filink_status_handler, ilink);
}

static void filink_msgbus_release(filink_t *ilink)
{
    fmsgbus_unsubscribe(ilink->msgbus, FNODE_STATUS, (fmsg_handler_t)filink_status_handler);
    fmsgbus_release(ilink->msgbus);
}

void fproto_node_status_handler(filink_t *ilink, fuuid_t const *uuid, uint32_t status)
{
    fmsg_node_status_t const msg = { *uuid,  filink_status_from_proto(status) };
    fmsgbus_publish(ilink->msgbus, FNODE_STATUS, &msg, sizeof msg);
}

static void filink_clients_accepter(fnet_server_t const *pserver, fnet_client_t *pclient)
{
    filink_t *ilink = (filink_t *)fnet_server_get_param(pserver);

    if (pclient)
    {
        fuuid_t peer_uuid;
        if (fproto_client_handshake_response(pclient, &ilink->uuid, &peer_uuid))
        {
            if (!filink_add_node(ilink, pclient, &peer_uuid))
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
    fnet_client_t *clients[FILINK_MAX_ALLOWED_CONNECTIONS_NUM];
    fnet_client_t *rclients[FILINK_MAX_ALLOWED_CONNECTIONS_NUM];
    fnet_client_t *eclients[FILINK_MAX_ALLOWED_CONNECTIONS_NUM];

    while(ilink->is_active)
    {
        filink_push_lock(ilink->nodes_mutex);
        ilink->wait_handler = fnet_wait_handler();
        for(size_t i = 0; i < ilink->nodes_num; ++i)
            clients[i] = ilink->nodes[i].transport;
        clients_num = ilink->nodes_num;
        filink_pop_lock();

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

filink_t *filink_bind(fmsgbus_t *pmsgbus, char const *addr, fuuid_t const *uuid)
{
    if (!pmsgbus)
    {
        FS_ERR("Invalid messages bus");
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

    ilink->uuid = *uuid;

    filink_msgbus_retain(ilink, pmsgbus);

    ilink->proto_handlers.param = ilink;
    ilink->proto_handlers.node_status_handler = (fproto_node_status_handler_t)fproto_node_status_handler;

    ilink->server = fnet_bind(FNET_SSL, addr, filink_clients_accepter);
    if (!ilink->server)
    {
        FS_ERR("Unable to bind the interlink");
        filink_unbind(ilink);
        return 0;
    }

    fnet_server_set_param(ilink->server, ilink);

    ilink->nodes_num = 0;

    static pthread_mutex_t mutex_initializer = PTHREAD_MUTEX_INITIALIZER;
    ilink->nodes_mutex = mutex_initializer;

    int rc = pthread_create(&ilink->thread, 0, filink_thread, (void*)ilink);
    if (rc)
    {
        FS_ERR("Unable to create the thread for clients processing. Error: %d", rc);
        filink_unbind(ilink);
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

        for(int i = 0; i < sizeof ilink->nodes / sizeof *ilink->nodes; ++i)
        {
            if (ilink->nodes[i].transport)
                fnet_disconnect(ilink->nodes[i].transport);
        }

        filink_msgbus_release(ilink);

        free(ilink);
    }
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
        FS_ERR("Invalid address");
        return false;
    }

    if (ilink->nodes_num >= FILINK_MAX_ALLOWED_CONNECTIONS_NUM)
    {
        FS_ERR("The maximum allowed connections number is reached");
        return false;
    }

    fnet_client_t *pclient = fnet_connect(FNET_SSL, addr);
    if (!pclient) return false;

    fuuid_t peer_uuid;
    if (!fproto_client_handshake_request(pclient, &ilink->uuid, &peer_uuid))
    {
        fnet_disconnect(pclient);
        return false;
    }

    return filink_add_node(ilink, pclient, &peer_uuid);
}
