#ifndef SSL_TRANSPORT_H_FNET
#define SSL_TRANSPORT_H_FNET

typedef struct fnet_ssl_client fnet_ssl_client_t;
typedef struct fnet_ssl_server fnet_ssl_server_t;
typedef void (*fnet_ssl_accepter_t)(fnet_ssl_server_t const *, fnet_ssl_client_t *);

fnet_ssl_client_t *fnet_ssl_connect(char const *addr);
void               fnet_ssl_disconnect(fnet_ssl_client_t *);
fnet_ssl_server_t *fnet_ssl_bind(char const *addr, fnet_ssl_accepter_t accepter);
void               fnet_ssl_unbind(fnet_ssl_server_t *);
void               fnet_ssl_server_set_userdata(fnet_ssl_server_t *, void *);
void              *fnet_ssl_server_get_userdata(fnet_ssl_server_t const *);

#endif
