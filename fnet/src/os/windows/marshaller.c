#include "../../marshaller.h"
#include <Winsock2.h>

bool fmarshal(fnet_client_t *client, uint8_t const *data, size_t size)
{
    return fnet_send(client, data, size);
}

bool fmarshal_u8(fnet_client_t *client, uint8_t v)
{
    return fnet_send(client, &v, sizeof v);
}

bool fmarshal_u16(fnet_client_t *client, uint16_t v)
{
    v = htons(v);
    return fnet_send(client, &v, sizeof v);
}

bool fmarshal_u32(fnet_client_t *client, uint32_t v)
{
    v = htonl(v);
    return fnet_send(client, &v, sizeof v);
}

bool fmarshal_u64(fnet_client_t *client, uint64_t v)
{
    #define fhtonll(x) ((1==htonl(1)) ? (x) : ((uint64_t)htonl((x) & 0xFFFFFFFF) << 32) | htonl((x) >> 32))
    v = fhtonll(v);
    #undef fhtonll
    return fnet_send(client, &v, sizeof v);
}

bool fmarshal_uuid(fnet_client_t *client, fuuid_t const *v)
{
    return fnet_send(client, v, sizeof *v);
}

bool fmarshal_bool(fnet_client_t *client, bool v)
{
    uint8_t p = v ? 1 : 0;
    return fnet_send(client, &p, sizeof p);
}

bool funmarshal(fnet_client_t *client, uint8_t *data, size_t size)
{
    return fnet_recv(client, data, size);
}

bool funmarshal_u8(fnet_client_t *client, uint8_t *v)
{
    return fnet_recv(client, v, sizeof *v);
}

bool funmarshal_u16(fnet_client_t *client, uint16_t *v)
{
    if (!fnet_recv(client, v, sizeof *v))
        return false;
    *v = ntohs(*v);
    return true;
}

bool funmarshal_u32(fnet_client_t *client, uint32_t *v)
{
    if (!fnet_recv(client, v, sizeof *v))
        return false;
    *v = ntohl(*v);
    return true;
}

bool funmarshal_u64(fnet_client_t *client, uint64_t *v)
{
    if (!fnet_recv(client, v, sizeof *v))
        return false;
    #define fntohll(x) ((1==ntohl(1)) ? (x) : ((uint64_t)ntohl((x) & 0xFFFFFFFF) << 32) | ntohl((x) >> 32))
    *v = fntohll(*v);
    #undef fntohll
    return true;
}

bool funmarshal_uuid(fnet_client_t *client, fuuid_t *v)
{
    return fnet_recv(client, v, sizeof *v);
}

bool funmarshal_bool(fnet_client_t *client, bool *v)
{
    uint8_t p = 0;
    if (!fnet_recv(client, &p, sizeof p))
        return false;
    *v = p ? true : false;
    return true;
}
