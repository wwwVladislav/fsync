#ifndef TRANSPORT_H_FNET
#define TRANSPORT_H_FNET

typedef struct fnet_client fnet_client_t;
typedef struct fnet_server fnet_server_t;

fnet_client_t *fnet_connect(char const *addr);
void           fnet_disconnect(fnet_client_t *);
fnet_server_t *fnet_bind(short port);
fnet_client_t *fnet_accept(fnet_server_t *);
void           fnet_unbind(fnet_server_t *);

#endif
