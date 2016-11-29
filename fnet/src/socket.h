#ifndef SOCKET_H_FNET
#define SOCKET_H_FNET
#include "ip_address.h"

int  fnet_sock_connect(fnet_address_t const *addr);
void fnet_sock_disconnect(int sock);

#endif
