#include "interface.h"
#include "protocol.h"
#include <fnet/transport.h>
#include <futils/log.h>
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
    volatile bool       is_active;
    fuuid_t             uuid;
    pthread_t           thread;
    fnet_server_t      *server;
    pthread_mutex_t     nodes_mutex;
    volatile size_t     nodes_num;
    node_t              nodes[FILINK_MAX_ALLOWED_CONNECTIONS_NUM];
    fnet_wait_handler_t wait_handler;
};

#define filink_push_lock(mutex)                     \
    if (pthread_mutex_lock(&mutex))	            \
        FS_ERR("The mutex locking is failed");      \
    else                                            \
        pthread_cleanup_push((void (*)())pthread_mutex_unlock, (void *)&mutex);

#define filink_pop_lock() pthread_cleanup_pop(1);

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

static void filink_clients_accepter(fnet_server_t const *pserver, fnet_client_t *pclient)
{
    filink_t *ilink = (filink_t *)fnet_server_get_userdata(pserver);

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

        // TODO: handle clients
    }

    return 0;
}

filink_t *filink_bind(char const *addr, fuuid_t const *uuid)
{
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

    ilink->server = fnet_bind(FNET_SSL, addr, filink_clients_accepter);
    if (!ilink->server)
    {
        FS_ERR("Unable to bind the interlink");
        filink_unbind(ilink);
        return 0;
    }

    fnet_server_set_userdata(ilink->server, ilink);

    ilink->nodes_num = 0;

    static pthread_mutex_t mutex_initializer = PTHREAD_MUTEX_INITIALIZER;
    ilink->nodes_mutex = mutex_initializer;

    int rc = pthread_create(&ilink->thread, 0, filink_thread, (void*)ilink);
    if (rc)
    {
        FS_ERR("Unable to create the thread for clints processing. Error: %d", rc);
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
