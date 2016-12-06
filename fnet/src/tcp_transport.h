#ifndef TCP_TRANSPORT_H_FNET
#define TCP_TRANSPORT_H_FNET

typedef struct fnet_tcp_client fnet_tcp_client_t;
typedef struct fnet_tcp_server fnet_tcp_server_t;
typedef void (*fnet_tcp_accepter_t)(fnet_tcp_client_t *);

fnet_tcp_client_t *fnet_tcp_connect(char const *addr);
void               fnet_tcp_disconnect(fnet_tcp_client_t *);
fnet_tcp_server_t *fnet_tcp_bind(char const *addr, fnet_tcp_accepter_t accepter);
void               fnet_tcp_unbind(fnet_tcp_server_t *);

#endif
