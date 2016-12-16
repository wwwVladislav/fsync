#include "../../socket.h"
#include "../../config.h"
#include <futils/log.h>
#include <Winsock2.h>

fnet_socket_t const FNET_INVALID_SOCKET = (fnet_socket_t)~0u;

bool fnet_socket_init()
{
    WSADATA wsa_info;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_info)
        || wsa_info.wVersion != MAKEWORD(2, 2))
    {
        FS_ERR("Windows socket library initialization is failed");
        return false;
    }
    return true;
}

void fnet_socket_uninit()
{
    WSACleanup();
}

fnet_socket_t fnet_socket_create_dummy()
{
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET)
    {
        FS_ERR("Unable to create dummy socket");
        return FNET_INVALID_SOCKET;
    }
    return (fnet_socket_t)sock;
}

fnet_socket_t fnet_socket_connect(fnet_address_t const *addr)
{
    if (!addr)
    {
        FS_ERR("Invalid address");
        return FNET_INVALID_SOCKET;
    }

    SOCKET sock = socket(addr->ss_family, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET)
    {
        FS_ERR("Unable to create new socket");
        return FNET_INVALID_SOCKET;
    }

    if (connect(sock, (const struct sockaddr*)addr, sizeof *addr) == SOCKET_ERROR)
    {
        FS_ERR("Unable to establish new connection");
        closesocket(sock);
        return FNET_INVALID_SOCKET;
    }

    return (fnet_socket_t)sock;
}

void fnet_socket_shutdown(fnet_socket_t sock)
{
    if (sock != FNET_INVALID_SOCKET)
        shutdown((SOCKET)sock, SD_BOTH);
}

void fnet_socket_close(fnet_socket_t sock)
{
    if (sock != FNET_INVALID_SOCKET)
        closesocket((SOCKET)sock);
}

fnet_socket_t fnet_socket_bind(fnet_address_t const *addr)
{
    if (!addr)
    {
        FS_ERR("Invalid address");
        return FNET_INVALID_SOCKET;
    }

    SOCKET sock = socket(addr->ss_family, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET)
    {
        FS_ERR("Unable to create new socket");
        return FNET_INVALID_SOCKET;
    }

    if (bind(sock, (const struct sockaddr*)addr, sizeof *addr) == SOCKET_ERROR)
    {
        FS_ERR("Socket bind error");
        closesocket(sock);
        return FNET_INVALID_SOCKET;
    }

    if (listen(sock, FNET_SOCK_MAX_CONNECTIONS) == SOCKET_ERROR)
    {
        FS_ERR("Socket listen error");
        closesocket(sock);
        return FNET_INVALID_SOCKET;
    }

    return (fnet_socket_t)sock;
}

fnet_socket_t fnet_socket_accept(fnet_socket_t sock, fnet_address_t *addr)
{
    int addr_len = sizeof(fnet_address_t);
    SOCKET client_sock = accept((SOCKET)sock, (struct sockaddr *)addr, addr ? &addr_len : 0);
    if (client_sock == INVALID_SOCKET)
        return FNET_INVALID_SOCKET;
    return (fnet_socket_t)client_sock;
}

bool fnet_socket_send(fnet_socket_t sock, const char *buf, size_t len)
{
    if (!buf)
    {
        FS_ERR("Invalid buffer");
        return false;
    }

    do
    {
        int res = send((SOCKET)sock, buf, len, 0);
        if (res == SOCKET_ERROR)
        {
            FS_ERR("Unable to send data");
            return false;
        }
        buf += res;
        len -= res;
    }
    while (len > 0);

    return true;
}

bool fnet_socket_recv(fnet_socket_t sock, char *buf, size_t len, size_t *read_len)
{
    if (!read_len || !buf)
    {
        FS_ERR("Invalid arguments");
        return false;
    }

    *read_len = 0;

    do
    {
        int res = recv((SOCKET)sock, buf, len, 0);
        switch(res)
        {
            case SOCKET_ERROR:
            {
                FS_ERR("Unable to recv data");
                return false;
            }

            case 0:
            {
                // connection closed
                return false;
            }

            default:
                break;
        }

        *read_len += res;
        buf += res;
        len -= res;
    }
    while (len > 0);

    return true;
}

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

    if (rs)
    {
        FD_ZERO(&readfds);
        for(size_t i = 0; i < num; ++i)
            FD_SET((SOCKET)sockets[i], &readfds);
        *rs_num = 0;
    }

    if (es)
    {
        FD_ZERO(&exceptfds);
        for(size_t i = 0; i < num; ++i)
            FD_SET((SOCKET)sockets[i], &exceptfds);
        *es_num = 0;
    }

    int ret = select(0, rs ? &readfds : 0, 0, es ? &exceptfds : 0, 0);
    if (ret == SOCKET_ERROR)
    {
        FS_ERR("Socket waiting was failed");
        return false;
    }

    size_t rsi = 0;
    size_t esi = 0;

    for(size_t i = 0; i < num; ++i)
    {
        if (rs && FD_ISSET((SOCKET)sockets[i], &readfds))
            rs[rsi++] = sockets[i];
        if (es && FD_ISSET((SOCKET)sockets[i], &exceptfds))
            es[esi++] = sockets[i];
    }

    if (rs_num) *rs_num = rsi;
    if (es_num) *es_num = esi;
    return true;
}
