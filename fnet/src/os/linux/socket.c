#include "../../socket.h"
#include <fcommon/limits.h>
#include <futils/log.h>
#include <sys/select.h>
#include <unistd.h>

fnet_socket_t const FNET_INVALID_SOCKET = (fnet_socket_t)~0u;

bool fnet_socket_init()
{
    return true;
}

void fnet_socket_uninit()
{}

bool fnet_socket_select(fnet_socket_t *sockets,
                        size_t num,
                        fnet_socket_t *rs,
                        size_t *rs_num,
                        fnet_socket_t *es,
                        size_t *es_num)
{
    if (!sockets || !num)   return false;
    if (!rs && !es)         return false;
    if (rs && !rs_num)      return false;
    if (es && !es_num)      return false;

    fd_set readfds;
    fd_set exceptfds;
    fnet_socket_t nfds = 0;

    for(size_t i = 0; i < num; ++i)
        nfds = nfds < sockets[i] ? sockets[i] : nfds;

    if (rs)
    {
        FD_ZERO(&readfds);
        for(size_t i = 0; i < num; ++i)
            FD_SET(sockets[i], &readfds);
        *rs_num = 0;
    }

    if (es)
    {
        FD_ZERO(&exceptfds);
        for(size_t i = 0; i < num; ++i)
            FD_SET(sockets[i], &exceptfds);
        *es_num = 0;
    }

    int ret = select(nfds + 1, rs ? &readfds : 0, 0, es ? &exceptfds : 0, 0);
    if (ret < 0)
    {
        FS_ERR("Socket waiting was failed");
        return false;
    }

    size_t rsi = 0;
    size_t esi = 0;

    for(size_t i = 0; i < num; ++i)
    {
        if (rs && FD_ISSET(sockets[i], &readfds))
            rs[rsi++] = sockets[i];
        if (es && FD_ISSET(sockets[i], &exceptfds))
            es[esi++] = sockets[i];
    }

    if (rs_num) *rs_num = rsi;
    if (es_num) *es_num = esi;
    return true;
}

void fnet_socket_close(fnet_socket_t sock)
{
    if (sock != FNET_INVALID_SOCKET)
        close((int)sock);
}

fnet_socket_t fnet_socket_bind(fnet_address_t const *addr)
{
    if (!addr)
    {
        FS_ERR("Invalid address");
        return FNET_INVALID_SOCKET;
    }

    int sock = socket(addr->ss_family, SOCK_STREAM, IPPROTO_TCP);
    if (sock == -1)
    {
        FS_ERR("Unable to create new socket");
        return FNET_INVALID_SOCKET;
    }

    if (bind(sock, (const struct sockaddr*)addr, sizeof *addr) == -1)
    {
        FS_ERR("Socket bind error");
        close(sock);
        return FNET_INVALID_SOCKET;
    }

    if (listen(sock, FMAX_ACCEPT_CONNECTIONS) == -1)
    {
        FS_ERR("Socket listen error");
        close(sock);
        return FNET_INVALID_SOCKET;
    }

    return (fnet_socket_t)sock;
}

fnet_socket_t fnet_socket_accept(fnet_socket_t sock, fnet_address_t *addr)
{
    socklen_t addr_len = sizeof(fnet_address_t);
    int client_sock = accept((int)sock, (struct sockaddr *)addr, addr ? &addr_len : 0);
    if (client_sock == -1)
        return FNET_INVALID_SOCKET;
    return (fnet_socket_t)client_sock;
}

void fnet_socket_shutdown(fnet_socket_t sock)
{
    if (sock != FNET_INVALID_SOCKET)
        shutdown((int)sock, SHUT_RDWR);
}

fnet_socket_t fnet_socket_connect(fnet_address_t const *addr)
{
    if (!addr)
    {
        FS_ERR("Invalid address");
        return FNET_INVALID_SOCKET;
    }

    int sock = socket(addr->ss_family, SOCK_STREAM, IPPROTO_TCP);
    if (sock == -1)
    {
        FS_ERR("Unable to create new socket");
        return FNET_INVALID_SOCKET;
    }

    if (connect(sock, (const struct sockaddr*)addr, sizeof *addr) == -1)
    {
        FS_ERR("Unable to establish new connection");
        close(sock);
        return FNET_INVALID_SOCKET;
    }

    return (fnet_socket_t)sock;
}

fnet_socket_t fnet_socket_create_dummy()
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == -1)
    {
        FS_ERR("Unable to create dummy socket");
        return FNET_INVALID_SOCKET;
    }
    return (fnet_socket_t)sock;
}
