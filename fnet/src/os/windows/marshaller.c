#include "../../marshaller.h"
#include <Winsock2.h>

bool fmarshal(fnet_client_t *client, uint8_t const *data, size_t size)
{
    return fnet_send(client, data, size);
}

bool fmarshal_u32(fnet_client_t *client, uint32_t v)
{
    v = htonl(v);
    return fnet_send(client, &v, sizeof v);
}

bool fmarshal_uuid(fnet_client_t *client, fuuid_t const *v)
{
    return fnet_send(client, v, sizeof *v);
}

bool funmarshal(fnet_client_t *client, uint8_t *data, size_t size)
{
    return fnet_recv(client, data, size);
}

bool funmarshal_u32(fnet_client_t *client, uint32_t *v)
{
    if (!fnet_recv(client, v, sizeof *v))
        return false;
    *v = ntohl(*v);
    return true;
}

bool funmarshal_uuid(fnet_client_t *client, fuuid_t *v)
{
    return fnet_recv(client, v, sizeof *v);
}
