#include "tcp_transport.h"
#include "socket.h"
#include "ip_address.h"
#include <futils/log.h>
#include <pthread.h>
#include <time.h>
#include <stdlib.h>

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
    void               *user_data;
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

void fnet_tcp_server_set_userdata(fnet_tcp_server_t *pserver, void *pdata)
{
    pserver->user_data = pdata;
}

void *fnet_tcp_server_get_userdata(fnet_tcp_server_t const *pserver)
{
    return pserver->user_data;
}
