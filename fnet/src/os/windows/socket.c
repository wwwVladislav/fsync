#include "../../socket.h"
#include <Winsock2.h>
#include <futils/log.h>
#include <stdbool.h>

volatile static unsigned net_init_count = 0;

static bool fnet_init()
{
    if (!net_init_count)
    {
        WSADATA wsaInfo;
        int err = WSAStartup(MAKEWORD(2, 2), &wsaInfo);
        if(err)
        {
            FS_ERR("WSAStartup failed. Error: %d", err);
            return false;
        }
        if(wsaInfo.wVersion != MAKEWORD(2, 2))
        {
            FS_ERR("WSA version 2.2 is not supported");
            return false;
        }
    }
    ++net_init_count;
    return true;
}

static void fnet_uninit()
{
    --net_init_count;
    if (!net_init_count)
        WSACleanup();
}

int fnet_sock_connect(fnet_address_t const *addr)
{
    if (!addr)
    {
        FS_ERR("Invalid address");
        return -1;
    }

    if (!fnet_init())
        return -1;

    int sd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sd == -1)
    {
        FS_ERR("Unable to create the new socket");
        fnet_uninit();
        return -1;
    }

    switch (addr->ss_family)
    {
        case AF_INET:
        {
            struct sockaddr_in *p = (struct sockaddr_in *)addr;
            if (connect(sd, (SOCKADDR*)p, sizeof(struct sockaddr_in)) == SOCKET_ERROR)
            {
                closesocket(sd);
                fnet_uninit();
                FS_ERR("Unable to connect to remote host");
                return -1;
            }
            break;
        }

        case AF_INET6:
        {
            struct sockaddr_in6 *p = (struct sockaddr_in6*)addr;
            if (connect(sd, (SOCKADDR*)p, sizeof(struct sockaddr_in6)) == SOCKET_ERROR)
            {
                closesocket(sd);
                fnet_uninit();
                FS_ERR("Unable to connect to remote host");
                return -1;
            }
            break;
        }

        default:
        {
            closesocket(sd);
            fnet_uninit();
            FS_ERR("Unsupported protocol family");
            return -1;
        }
    }

    return sd;
}

void fnet_sock_disconnect(int sock)
{
    if (sock != -1)
    {
        shutdown(sock, SD_BOTH);
        closesocket(sock);
        fnet_uninit();
    }
}
