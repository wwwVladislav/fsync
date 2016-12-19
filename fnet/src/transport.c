#include "transport.h"
#include "tcp_transport.h"
#include "ssl_transport.h"
#include <futils/log.h>
 #include <stdlib.h>
#include <string.h>

typedef void               (*fnet_disconnect_t)   (void *);
typedef void               (*fnet_unbind_t)       (void *);
typedef fnet_tcp_client_t* (*fnet_get_transport_t)(void *);
typedef bool               (*fnet_send_t)         (void *, const void *, size_t);
typedef bool               (*fnet_recv_t)         (fnet_client_t *, void *, size_t);

struct fnet_client
{
    fnet_transport_t     transport;
    void                *pimpl;
    fnet_disconnect_t    disconnect;
    fnet_get_transport_t get_transoport;
    fnet_send_t          send;
    fnet_recv_t          recv;
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
            pclient->transport = FNET_TCP;
            pclient->pimpl = fnet_tcp_connect(addr);
            if (!pclient->pimpl)
            {
                fnet_disconnect(pclient);
                return 0;
            }
            pclient->disconnect = (fnet_disconnect_t)fnet_tcp_disconnect;
            pclient->get_transoport = (fnet_get_transport_t)fnet_tcp_get_transport;
            pclient->send = (fnet_send_t)fnet_tcp_send;
            pclient->recv = (fnet_recv_t)fnet_tcp_recv;
            break;
        }

        case FNET_SSL:
        {
            pclient->transport = FNET_SSL;
            pclient->pimpl = fnet_ssl_connect(addr);
            if (!pclient->pimpl)
            {
                fnet_disconnect(pclient);
                return 0;
            }
            pclient->disconnect = (fnet_disconnect_t)fnet_ssl_disconnect;
            pclient->get_transoport = (fnet_get_transport_t)fnet_ssl_get_transport;
            pclient->send = (fnet_send_t)fnet_ssl_send;
            pclient->recv = (fnet_recv_t)fnet_ssl_recv;
            break;
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
        pclient->transport = FNET_TCP;
        pclient->pimpl = tcp_client;
        pclient->disconnect = (fnet_disconnect_t)fnet_tcp_disconnect;
        pclient->get_transoport = (fnet_get_transport_t)fnet_tcp_get_transport;
        pclient->send = (fnet_send_t)fnet_tcp_send;
        pclient->recv = (fnet_recv_t)fnet_tcp_recv;

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
        pclient->transport = FNET_SSL;
        pclient->pimpl = ssl_client;
        pclient->disconnect = (fnet_disconnect_t)fnet_ssl_disconnect;
        pclient->get_transoport = (fnet_get_transport_t)fnet_ssl_get_transport;
        pclient->send = (fnet_send_t)fnet_ssl_send;
        pclient->recv = (fnet_recv_t)fnet_ssl_recv;

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

fnet_wait_handler_t fnet_wait_handler()
{
    return (fnet_wait_handler_t)fnet_tcp_wait_handler();
}

void fnet_wait_cancel(fnet_wait_handler_t h)
{
    fnet_tcp_wait_cancel((fnet_tcp_wait_handler_t)h);
}

bool fnet_select(fnet_client_t **clients,
                 size_t clients_num,
                 fnet_client_t **rclients,
                 size_t *rclients_num,
                 fnet_client_t **eclients,
                 size_t *eclients_num,
                 fnet_wait_handler_t wait_handler)
{
    fnet_tcp_client_t *tcp_clients[clients_num];
    fnet_tcp_client_t *rtcp_clients[clients_num];
    fnet_tcp_client_t *etcp_clients[clients_num];
    size_t rs_num = 0;
    size_t es_num = 0;

    for (size_t i = 0; i < clients_num; ++i)
        tcp_clients[i] = clients[i]->get_transoport(clients[i]->pimpl);

    if (!fnet_tcp_select(tcp_clients,
                         clients_num,
                         rclients ? rtcp_clients : 0,
                         rclients ? &rs_num : 0,
                         eclients ? etcp_clients : 0,
                         eclients ? &es_num : 0,
                         (fnet_tcp_wait_handler_t)wait_handler))
        return false;

    if (rclients)
    {
        size_t j = 0, n = 0;
        for(size_t i = 0; i < rs_num; ++i)
        {
            for(; j < clients_num && tcp_clients[j] != rtcp_clients[i]; ++j);
            if (j < clients_num)
                rclients[n++] = clients[j];
            j++;
        }
        *rclients_num = n;
    }

    if (eclients)
    {
        size_t j = 0, n = 0;
        for(size_t i = 0; i < es_num; ++i)
        {
            for(; j < clients_num && tcp_clients[j] != etcp_clients[i]; ++j);
            if (j < clients_num)
                eclients[n++] = clients[j];
            j++;
        }
        *eclients_num = n;
    }

    return true;
}

bool fnet_send(fnet_client_t *client, const void *buf, size_t len)
{
    return client->send(client->pimpl, buf, len);
}

bool fnet_recv(fnet_client_t *client, void *buf, size_t len)
{
    return client->recv(client->pimpl, buf, len);
}
