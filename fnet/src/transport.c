#include "transport.h"
#include "tcp_transport.h"
#include "ssl_transport.h"
#include <futils/log.h>
 #include <stdlib.h>
#include <string.h>

typedef void (*fnet_disconnect_t)(void *);
typedef void (*fnet_unbind_t)(void *);

struct fnet_client
{
    void             *pimpl;
    fnet_disconnect_t disconnect;
};

struct fnet_server
{
    fnet_accepter_t   accepter;
    void             *pimpl;
    fnet_unbind_t     unbind;
    void             *user_data;
};

fnet_client_t *fnet_connect(fnet_transport_t transport, char const *addr)
{
    if (!addr)
    {
        FS_ERR("Invalid address");
        return 0;
    }

    fnet_client_t *pclient = malloc(sizeof(fnet_client_t));
    if (!pclient)
    {
        FS_ERR("Unable to allocate memory for client");
        return 0;
    }
    memset(pclient, 0, sizeof *pclient);

    switch(transport)
    {
        case FNET_TCP:
        {
            pclient->pimpl = fnet_tcp_connect(addr);
            if (!pclient->pimpl)
            {
                fnet_disconnect(pclient);
                return 0;
            }
            pclient->disconnect = (fnet_disconnect_t)fnet_tcp_disconnect;
            break;
        }

        case FNET_SSL:
        {
            pclient->pimpl = fnet_ssl_connect(addr);
            if (!pclient->pimpl)
            {
                fnet_disconnect(pclient);
                return 0;
            }
            pclient->disconnect = (fnet_disconnect_t)fnet_ssl_disconnect;
        }

        default:
        {
            FS_ERR("Unsupported transport type");
            fnet_disconnect(pclient);
            return 0;
        }
    }

    return pclient;
}

void fnet_disconnect(fnet_client_t *pclient)
{
    if (pclient)
    {
        if (pclient->pimpl)
            pclient->disconnect(pclient->pimpl);
        free(pclient);
    }
    else FS_ERR("Invalid argument");
}

static void fnet_tcp_accepter(fnet_tcp_server_t const *tcp_server, fnet_tcp_client_t *tcp_client)
{
    fnet_client_t *pclient = malloc(sizeof(fnet_client_t));
    if (!pclient)
    {
        FS_ERR("Unable to allocate memory for client");
        fnet_tcp_disconnect(tcp_client);
    }
    else
    {
        memset(pclient, 0, sizeof *pclient);
        pclient->pimpl = tcp_client;
        pclient->disconnect = (fnet_disconnect_t)fnet_tcp_disconnect;

        fnet_server_t *pserver = (fnet_server_t*)fnet_tcp_server_get_userdata(tcp_server);
        pserver->accepter(pserver, pclient);
    }
}

static void fnet_ssl_accepter(fnet_ssl_server_t const *ssl_server, fnet_ssl_client_t *ssl_client)
{
    fnet_client_t *pclient = malloc(sizeof(fnet_client_t));
    if (!pclient)
    {
        FS_ERR("Unable to allocate memory for client");
        fnet_ssl_disconnect(ssl_client);
    }
    else
    {
        memset(pclient, 0, sizeof *pclient);
        pclient->pimpl = ssl_client;
        pclient->disconnect = (fnet_disconnect_t)fnet_ssl_disconnect;

        fnet_server_t *pserver = (fnet_server_t*)fnet_ssl_server_get_userdata(ssl_server);
        pserver->accepter(pserver, pclient);
    }
}

fnet_server_t *fnet_bind(fnet_transport_t transport, char const *addr, fnet_accepter_t accepter)
{
    if (!addr)
    {
        FS_ERR("Invalid address");
        return 0;
    }

    fnet_server_t *pserver = malloc(sizeof(fnet_server_t));
    if (!pserver)
    {
        FS_ERR("Unable to allocate memory for server");
        return 0;
    }
    memset(pserver, 0, sizeof *pserver);

    pserver->accepter = accepter;

    switch(transport)
    {
        case FNET_TCP:
        {
            pserver->pimpl = fnet_tcp_bind(addr, fnet_tcp_accepter);
            if (!pserver->pimpl)
            {
                fnet_unbind(pserver);
                return 0;
            }

            fnet_tcp_server_set_userdata(pserver->pimpl, pserver);

            pserver->unbind = (fnet_unbind_t)fnet_tcp_unbind;
            break;
        }

        case FNET_SSL:
        {
            pserver->pimpl = fnet_ssl_bind(addr, fnet_ssl_accepter);
            if (!pserver->pimpl)
            {
                fnet_unbind(pserver);
                return 0;
            }

            fnet_ssl_server_set_userdata(pserver->pimpl, pserver);

            pserver->unbind = (fnet_unbind_t)fnet_ssl_unbind;
            break;
        }

        default:
        {
            FS_ERR("Unsupported transport type");
            fnet_unbind(pserver);
            return 0;
        }
    }

    return pserver;
}

void fnet_unbind(fnet_server_t *pserver)
{
    if (pserver)
    {
        if (pserver->unbind)
            pserver->unbind(pserver->pimpl);
        free(pserver);
    }
    else FS_ERR("Invalid argument");
}

void fnet_server_set_userdata(fnet_server_t *pserver, void *pdata)
{
    pserver->user_data = pdata;
}

void *fnet_server_get_userdata(fnet_server_t const *pserver)
{
    return pserver->user_data;
}
