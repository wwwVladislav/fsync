#ifndef TRANSPORT_H_FNET
#define TRANSPORT_H_FNET

typedef struct fnet_client fnet_client_t;
typedef struct fnet_server fnet_server_t;
typedef void (*fnet_accepter_t)(fnet_client_t *);

fnet_client_t *fnet_connect(char const *addr);
void           fnet_disconnect(fnet_client_t *);
fnet_server_t *fnet_bind(char const *addr, fnet_accepter_t accepter);
void           fnet_unbind(fnet_server_t *);

#endif
