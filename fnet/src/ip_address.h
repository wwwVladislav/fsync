#ifndef IP_ADDRESS_H_FNET
#define IP_ADDRESS_H_FNET
#ifdef _WIN32
#   include <Winsock2.h>
#   include <Ws2tcpip.h>
#else
#   include <arpa/inet.h>
#endif

typedef struct sockaddr_storage fnet_address_t;

#endif
