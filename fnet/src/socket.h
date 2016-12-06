#ifndef SOCKET_H_FNET
#define SOCKET_H_FNET
#include <stdbool.h>
#include <stddef.h>
#include "ip_address.h"

typedef unsigned fnet_socket_t;

extern fnet_socket_t const FNET_INVALID_SOCKET;

bool            fnet_socket_init();
void            fnet_socket_uninit();
fnet_socket_t   fnet_socket_connect(fnet_address_t const *addr);
void            fnet_socket_shutdown(fnet_socket_t sock);
void            fnet_socket_close(fnet_socket_t sock);
fnet_socket_t   fnet_socket_bind(fnet_address_t const *addr);
fnet_socket_t   fnet_socket_accept(fnet_socket_t sock, fnet_address_t *addr);
bool            fnet_socket_send(fnet_socket_t sock, const char *buf, size_t len);
bool            fnet_socket_recv(fnet_socket_t sock, char *buf, size_t len, size_t *read_len);

#endif
