#include "module.h"
#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <futils/log.h>

volatile static int fnet_ref_count = 0;

bool fnet_init()
{
    if (!fnet_ref_count)
    {
        SSL_load_error_strings();
        ERR_load_BIO_strings();
        OpenSSL_add_all_algorithms();
    }
    ++fnet_ref_count;
    return true;
}

void fnet_uninit()
{
    if (fnet_ref_count)
    {
        --fnet_ref_count;
        if (!fnet_ref_count)
        {
            ERR_free_strings();
            EVP_cleanup();
        }
    }
}
