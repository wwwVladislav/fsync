#ifndef TRANSPORT_H_FNET
#define TRANSPORT_H_FNET
#include <stddef.h>
#include <stdbool.h>

typedef struct fnet_client fnet_client_t;
typedef struct fnet_server fnet_server_t;
typedef unsigned fnet_wait_handler_t;
typedef void (*fnet_accepter_t)(fnet_server_t const *, fnet_client_t *);

typedef enum
{
    FNET_TCP = 0,
    FNET_SSL
} fnet_transport_t;

fnet_client_t      *fnet_connect(fnet_transport_t transport, char const *addr);
void                fnet_disconnect(fnet_client_t *);
fnet_server_t      *fnet_bind(fnet_transport_t transport, char const *addr, fnet_accepter_t accepter);
void                fnet_unbind(fnet_server_t *);
void                fnet_server_set_userdata(fnet_server_t *, void *);
void               *fnet_server_get_userdata(fnet_server_t const *);
fnet_wait_handler_t fnet_wait_handler();
void                fnet_wait_cancel(fnet_wait_handler_t);
bool                fnet_select(fnet_client_t **clients,
                                size_t clients_num,
                                fnet_client_t **rclients,
                                size_t *rclients_num,
                                fnet_client_t **eclients,
                                size_t *eclients_num,
                                fnet_wait_handler_t wait_handler);
bool                fnet_send(fnet_client_t *client, const void *buf, size_t len);
bool                fnet_recv(fnet_client_t *client, void *buf, size_t len);
bool                fnet_acquire(fnet_client_t *client);
void                fnet_release(fnet_client_t *client);

#endif
