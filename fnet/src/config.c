#include "config.h"

// !!! Prepares a folder for use as the path parameter: "c_rehash ./certificates"
char const *FNET_TRUSTED_CERTS_DIR  = "./certificates";
char const *FNET_CERTIFICATE_FILE   = "node.cert.pem";
char const *FNET_PRIVATE_KEY_FILE   = "node.key.pem";
char const *FNET_DEFAULT_PASSWORD   = "123456";
unsigned FNET_SOCK_MAX_CONNECTIONS  = 10u;
