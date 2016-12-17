#ifndef TCP_TRANSPORT_H_FNET
#define TCP_TRANSPORT_H_FNET
#include <stddef.h>
#include <stdbool.h>

typedef struct fnet_tcp_client fnet_tcp_client_t;
typedef struct fnet_tcp_server fnet_tcp_server_t;
typedef unsigned fnet_tcp_wait_handler_t;
typedef void (*fnet_tcp_accepter_t)(fnet_tcp_server_t const *, fnet_tcp_client_t *);

fnet_tcp_client_t      *fnet_tcp_connect(char const *addr);
void                    fnet_tcp_disconnect(fnet_tcp_client_t *);
fnet_tcp_server_t      *fnet_tcp_bind(char const *addr, fnet_tcp_accepter_t accepter);
void                    fnet_tcp_unbind(fnet_tcp_server_t *);
void                    fnet_tcp_server_set_userdata(fnet_tcp_server_t *, void *);
void                   *fnet_tcp_server_get_userdata(fnet_tcp_server_t const *);
fnet_tcp_wait_handler_t fnet_tcp_wait_handler();
void                    fnet_tcp_wait_cancel(fnet_tcp_wait_handler_t);
bool                    fnet_tcp_select(fnet_tcp_client_t **clients,
                                        size_t clients_num,
                                        fnet_tcp_client_t **rclients,
                                        size_t *rclients_num,
                                        fnet_tcp_client_t **eclients,
                                        size_t *eclients_num,
                                        fnet_tcp_wait_handler_t wait_handler);

#endif
