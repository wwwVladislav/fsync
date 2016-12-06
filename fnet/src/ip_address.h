#ifndef IP_ADDRESS_H_FNET
#define IP_ADDRESS_H_FNET
#ifdef _WIN32
#   define WIN32_LEAN_AND_MEAN
#   define _WIN32_WINNT   0x0501
#   include <Winsock2.h>
#   include <Ws2tcpip.h>
#else
#   include <arpa/inet.h>
#endif
#include <stdbool.h>

typedef struct sockaddr_storage fnet_address_t;

bool fnet_str2addr(char const *str, fnet_address_t *addr);

#endif
