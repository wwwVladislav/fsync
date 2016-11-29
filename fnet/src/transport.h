#ifndef TRANSPORT_H_FNET
#define TRANSPORT_H_FNET
#include "ip_address.h"

typedef struct fnet_client fnet_client_t;
typedef struct fnet_server fnet_server_t;

fnet_client_t *fnet_connect(fnet_address_t const *addr);
void           fnet_disconnect(fnet_client_t *);
fnet_server_t *fnet_bind(short port);
fnet_client_t *fnet_accept(fnet_server_t *);
void           fnet_unbind(fnet_server_t *);

#endif
