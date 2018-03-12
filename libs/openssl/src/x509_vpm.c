#include <openssl/x509_vfy.h>

void X509_VERIFY_PARAM_set_hostflags(X509_VERIFY_PARAM *param, unsigned int flags)
{
    (void)param;
    (void)flags;
}

int X509_VERIFY_PARAM_set1_host(X509_VERIFY_PARAM *param, const char *name, size_t namelen)
{
    (void)param;
    (void)name;
    (void)namelen;
    return 0;
}
