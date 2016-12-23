#include "tcp_transport.h"
#include "socket.h"
#include "ip_address.h"
#include <futils/log.h>
#include <pthread.h>
#include <time.h>
#include <stdlib.h>
#include <assert.h>

struct fnet_tcp_client
{
    fnet_socket_t  sock;
    fnet_address_t addr;
};

struct fnet_tcp_server
{
    volatile bool       is_active;
    fnet_socket_t       sock;
    pthread_t           thread;
    fnet_tcp_accepter_t accepter;
    void               *param;
};

typedef struct
{
    volatile int ref_count;
} fnet_tcp_module_t;

static fnet_tcp_module_t fnet_tcp_module = { 0 };

fnet_socket_t fnet_tcp_client_socket(fnet_tcp_client_t const *pclient)
{
    return pclient->sock;
}

static bool fnet_tcp_module_init(fnet_tcp_module_t *pmodule)
{
    if (!pmodule->ref_count)
    {
        if (!fnet_socket_init())
            return false;
    }
    pmodule->ref_count++;
    return true;
}

static void fnet_tcp_module_uninit(fnet_tcp_module_t *pmodule)
{
    if (pmodule->ref_count
        && !--pmodule->ref_count)
    {
        fnet_socket_uninit();
    }
}

fnet_tcp_client_t *fnet_tcp_connect(char const *addr)
{
    if (!addr)
    {
        FS_ERR("Invalid arguments");
        return 0;
    }

    fnet_address_t net_addr;
    if (!fnet_str2addr(addr, &net_addr))
    {
        FS_ERR("Invalid address");
        return 0;
    }

    fnet_tcp_client_t *pclient = malloc(sizeof(fnet_tcp_client_t));
    if (!pclient)
    {
        FS_ERR("Unable to allocate memory for client");
        return 0;
    }
    memset(pclient, 0, sizeof *pclient);
    pclient->sock = FNET_INVALID_SOCKET;
    pclient->addr = net_addr;

    if (!fnet_tcp_module_init(&fnet_tcp_module))
    {
        fnet_tcp_disconnect(pclient);
        return 0;
    }

    pclient->sock = fnet_socket_connect(&net_addr);
    if (pclient->sock == FNET_INVALID_SOCKET)
    {
        FS_ERR("Error establishing TCP connection.");
        fnet_tcp_disconnect(pclient);
        return 0;
    }

    return pclient;
}

void fnet_tcp_disconnect(fnet_tcp_client_t *pclient)
{
    if (pclient)
    {
        fnet_socket_shutdown(pclient->sock);
        fnet_socket_close(pclient->sock);
        fnet_tcp_module_uninit(&fnet_tcp_module);
        free(pclient);
    }
    else FS_ERR("Invalid argument");
}

static void *fnet_tcp_server_accept_thread(void *param)
{
    fnet_tcp_server_t *pserver = (fnet_tcp_server_t *)param;
    pserver->is_active = true;

    while(pserver->is_active)
    {
        fnet_address_t addr;
        fnet_socket_t sock = fnet_socket_accept(pserver->sock, &addr);

        if (!pserver->is_active)
            break;

        if (sock != FNET_INVALID_SOCKET)
        {
            if (fnet_tcp_module_init(&fnet_tcp_module))
            {
                fnet_tcp_client_t *pclient = (fnet_tcp_client_t*)malloc(sizeof(fnet_tcp_client_t));
                if (pclient)
                {
                    memset(pclient, 0, sizeof *pclient);
                    pclient->sock = sock;
                    pclient->addr = addr;
                    pserver->accepter(pserver, pclient);
                }
                else
                {
                    FS_ERR("Unable to allocate memory for client");
                    fnet_tcp_module_uninit(&fnet_tcp_module);
                }
            }
        }
    }

    return 0;
}

fnet_tcp_server_t *fnet_tcp_bind(char const *addr, fnet_tcp_accepter_t accepter)
{
    if (!addr || !accepter)
    {
        FS_ERR("Invalid arguments");
        return 0;
    }

    fnet_tcp_server_t *pserver = malloc(sizeof(fnet_tcp_server_t));
    if (!pserver)
    {
        FS_ERR("Unable to allocate memory for server");
        return 0;
    }
    memset(pserver, 0, sizeof *pserver);
    pserver->sock = FNET_INVALID_SOCKET;
    pserver->accepter = accepter;

    if (!fnet_tcp_module_init(&fnet_tcp_module))
    {
        free(pserver);
        return 0;
    }

    fnet_address_t net_addr;
    if (!fnet_str2addr(addr, &net_addr))
    {
        FS_ERR("Invalid address");
        fnet_tcp_unbind(pserver);
        return 0;
    }

    pserver->sock = fnet_socket_bind(&net_addr);
    if (pserver->sock == FNET_INVALID_SOCKET)
    {
        fnet_tcp_unbind(pserver);
        return 0;
    }

    int rc = pthread_create(&pserver->thread, 0, fnet_tcp_server_accept_thread, (void*)pserver);
    if (rc)
    {
        FS_ERR("Unable to create the thread to accept the client connections. Error: %d", rc);
        fnet_tcp_unbind(pserver);
        return 0;
    }

    static struct timespec const ts = { 1, 0 };
    while(!pserver->is_active)
        nanosleep(&ts, NULL);

    return pserver;
}

void fnet_tcp_unbind(fnet_tcp_server_t *pserver)
{
    if (pserver)
    {
        pserver->is_active = false;
        fnet_socket_close(pserver->sock);
        pthread_join(pserver->thread, 0);
        fnet_tcp_module_uninit(&fnet_tcp_module);
        free(pserver);
    }
    else FS_ERR("Invalid argument");
}

void fnet_tcp_server_set_param(fnet_tcp_server_t *pserver, void *param)
{
    pserver->param = param;
}

void *fnet_tcp_server_get_param(fnet_tcp_server_t const *pserver)
{
    return pserver->param;
}

fnet_tcp_client_t *fnet_tcp_get_transport(fnet_tcp_client_t *pclient)
{
    if (pclient)    return pclient;
    else            FS_ERR("Invalid argument");
    return 0;
}

fnet_tcp_wait_handler_t fnet_tcp_wait_handler()
{
    return (fnet_tcp_wait_handler_t)fnet_socket_create_dummy();
}

void fnet_tcp_wait_cancel(fnet_tcp_wait_handler_t h)
{
    fnet_socket_close((fnet_socket_t)h);
}

bool fnet_tcp_select(fnet_tcp_client_t **clients,
                     size_t clients_num,
                     fnet_tcp_client_t **rclients,
                     size_t *rclients_num,
                     fnet_tcp_client_t **eclients,
                     size_t *eclients_num,
                     fnet_tcp_wait_handler_t wait_handler)
{
    fnet_socket_t sockets[clients_num + 1];
    fnet_socket_t rsockets[clients_num + 1];
    fnet_socket_t esockets[clients_num + 1];
    size_t rs_num = 0;
    size_t es_num = 0;

    sockets[0] = wait_handler;
    for (size_t i = 0; i < clients_num; ++i)
        sockets[i + 1] = clients[i]->sock;

    if (!fnet_socket_select(sockets,
                            clients_num + 1,
                            rclients ? rsockets : 0,
                            rclients ? &rs_num : 0,
                            eclients ? esockets : 0,
                            eclients ? &es_num : 0))
        return false;

    if (rclients)
    {
        size_t j = 0, n = 0;
        for(size_t i = 0; i < rs_num; ++i)
        {
            for(; j < clients_num + 1 && sockets[j] != rsockets[i]; ++j);
            if (j != 0 && j <= clients_num)
                rclients[n++] = clients[j - 1];
            j++;
        }
        *rclients_num = n;
    }

    if (eclients)
    {
        size_t j = 0, n = 0;
        for(size_t i = 0; i < es_num; ++i)
        {
            for(; j < clients_num + 1 && sockets[j] != esockets[i]; ++j);
            if (j != 0 && j <= clients_num)
                eclients[n++] = clients[j - 1];
            j++;
        }
        *eclients_num = n;
    }

    return true;
}

bool fnet_tcp_send(fnet_tcp_client_t *client, const void *buf, size_t len)
{
    assert(0 && "TODO");
    return false;
}

bool fnet_tcp_recv(fnet_tcp_client_t *client, void *buf, size_t len)
{
    assert(0 && "TODO");
    return false;
}

bool fnet_tcp_acquire(fnet_tcp_client_t *client)
{
    (void)client;
    // It isn't implemented because TCP sockets support full-duplex concept.
    return true;
}

void fnet_tcp_release(fnet_tcp_client_t *client)
{
    (void)client;
}
