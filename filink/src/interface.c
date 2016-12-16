#include "interface.h"
#include <fnet/transport.h>
#include <futils/log.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

enum
{
    FILINK_MAX_ALLOWED_CONNECTIONS_NUM = 64
};

struct filink
{
    volatile bool  is_active;
    pthread_t      thread;
    fnet_server_t *server;
    fnet_client_t *clients[FILINK_MAX_ALLOWED_CONNECTIONS_NUM];
};

#define filink_push_lock(mutex)                     \
    if (pthread_mutex_lock(&mutex))	            \
        FS_ERR("The mutex locking is failed");      \
    else                                            \
        pthread_cleanup_push((void (*)())pthread_mutex_unlock, (void *)&mutex);

#define filink_pop_lock() pthread_cleanup_pop(1);

static void filink_clients_accepter(fnet_server_t const *pserver, fnet_client_t *pclient)
{
    filink_t *ilink = (filink_t *)fnet_server_get_userdata(pserver);
    // TODO: add to 'clients' array
}

static void *filink_thread(void *param)
{
    filink_t *ilink = (filink_t*)param;
    ilink->is_active = true;

    while(ilink->is_active)
    {
        // TODO
    }

    return 0;
}

filink_t *filink_bind(char const *addr)
{
    if (!addr)
    {
        FS_ERR("Invalid address");
        return 0;
    }

    filink_t *ilink = malloc(sizeof(filink_t));
    if (!ilink)
    {
        FS_ERR("Unable to allocate memory for nodes interlink");
        return 0;
    }
    memset(ilink, 0, sizeof *ilink);

    ilink->server = fnet_bind(FNET_SSL, addr, filink_clients_accepter);
    if (!ilink->server)
    {
        FS_ERR("Unable to bind the interlink");
        filink_unbind(ilink);
        return 0;
    }

    fnet_server_set_userdata(ilink->server, ilink);

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
        fnet_unbind(ilink->server);

        if (ilink->is_active)
        {
            ilink->is_active = false;
            pthread_join(ilink->thread, 0);
        }

        for(int i = 0; i < sizeof ilink->clients / sizeof *ilink->clients; ++i)
        {
            if (ilink->clients[i])
                fnet_disconnect(ilink->clients[i]);
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

    fnet_client_t *pclient = fnet_connect(FNET_SSL, addr);
    if (!pclient)
        return false;

    // TODO: add to 'clients' array

    return true;
}
