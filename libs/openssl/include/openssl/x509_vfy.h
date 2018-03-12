#ifndef X509_VFY_H_45656
#define X509_VFY_H_45656
#include "../../openssl/crypto/x509/x509_vfy.h"

#ifdef __cplusplus
extern "C" {
#endif

void X509_VERIFY_PARAM_set_hostflags(X509_VERIFY_PARAM *param, unsigned int flags);
int X509_VERIFY_PARAM_set1_host(X509_VERIFY_PARAM *param, const char *name, size_t namelen);

#ifdef __cplusplus
}
#endif

#endif
