#ifndef TRANSPORT_H_FNET
#define TRANSPORT_H_FNET
#include "ssl_transport.h"

typedef struct fnet_client fnet_client_t;
typedef struct fnet_server fnet_server_t;
typedef void (*fnet_accepter_t)(fnet_server_t const *, fnet_client_t *);

typedef enum
{
    FNET_TCP = 0,
    FNET_SSL
} fnet_transport_t;

fnet_client_t *fnet_connect(fnet_transport_t transport, char const *addr);
void           fnet_disconnect(fnet_client_t *);
fnet_server_t *fnet_bind(fnet_transport_t transport, char const *addr, fnet_accepter_t accepter);
void           fnet_unbind(fnet_server_t *);
void           fnet_server_set_userdata(fnet_server_t *, void *);
void          *fnet_server_get_userdata(fnet_server_t const *);

#endif
