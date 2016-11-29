#include "transport.h"
#include <futils/log.h>

struct fnet_client
{
    int sock;
};

struct fnet_server
{
    int tmp;
};

fnet_client_t *fnet_connect(fnet_address_t const *addr)
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

    // TODO

    return pclient;
}

void fnet_disconnect(fnet_client_t *pclient)
{
    if (!pclient)
    {
        FS_ERR("Invalid argument");
        return;
    }

    // TODO

    free(pclient);
}
