#include "transport.h"
#include "config.h"
#include <futils/log.h>
#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <stdbool.h>
#include <string.h>

struct fnet_client
{
    SSL *ssl;
    BIO *bio;
};

struct fnet_server
{
    SSL *ssl;
    BIO *bio;
    BIO *buffering_bio;
    BIO *accept_bio;
};

typedef struct
{
    volatile int ref_count;
    SSL_CTX *ctx;
} fnet_ssl_module_t;

static fnet_ssl_module_t fnet_ssl_module = { 0 };

static int fnet_pem_password_cb(char *buf, int size, int rwflag, void *userdata)
{
    (void)userdata;
    (void)rwflag;
    strncpy(buf, FNET_DEFAULT_PASSWORD, size);
    buf[size - 1] = '\0';
    return strlen(buf);
}

static bool fnet_ssl_module_init(fnet_ssl_module_t *pmodule)
{
    if (!pmodule->ref_count)
    {
        SSL_load_error_strings();
        ERR_load_BIO_strings();
        OpenSSL_add_all_algorithms();
        SSL_library_init();
        bool ret = false;

        do
        {
            SSL_METHOD const *method = SSLv23_method();
            if (!method)
            {
                FS_ERR("Unable to retrieve the SSL methods. Error: \'%s\'", ERR_reason_error_string(ERR_get_error()));
                break;
            }

            pmodule->ctx = SSL_CTX_new(method);
            if (!pmodule->ctx)
            {
                FS_ERR("Unable to create the SSL context. Error: \'%s\'", ERR_reason_error_string(ERR_get_error()));
                break;
            }

            SSL_CTX_set_options(pmodule->ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_COMPRESSION);

            if (!SSL_CTX_load_verify_locations(pmodule->ctx, 0, FNET_TRUSTED_CERTS_DIR))
            {
                FS_ERR("The trusted certificates loading is failed. Error: \'%s\'", ERR_reason_error_string(ERR_get_error()));
                break;
            }

            if (!SSL_CTX_use_certificate_file(pmodule->ctx, FNET_CERTIFICATE_FILE, SSL_FILETYPE_PEM))
            {
                FS_ERR("Error loading SSL certificate. Error: \'%s\'", ERR_reason_error_string(ERR_get_error()));
                break;
            }

            SSL_CTX_set_default_passwd_cb(pmodule->ctx, fnet_pem_password_cb);

            if (!SSL_CTX_use_PrivateKey_file(pmodule->ctx, FNET_PRIVATE_KEY_FILE, SSL_FILETYPE_PEM)
                || !SSL_CTX_check_private_key(pmodule->ctx))
            {
                FS_ERR("Error loading private key. Error: \'%s\'", ERR_reason_error_string(ERR_get_error()));
                break;
            }
            ret = true;
        }
        while(0);

        if (!ret)
        {
            if (pmodule->ctx)
            {
                SSL_CTX_free(pmodule->ctx);
                pmodule->ctx = 0;
            }
            ERR_free_strings();
            EVP_cleanup();
            return false;
        }
    }

    pmodule->ref_count++;
    return true;
}

static void fnet_ssl_module_uninit(fnet_ssl_module_t *pmodule)
{
    if (pmodule->ref_count
        && !--pmodule->ref_count)
    {
        SSL_CTX_free(pmodule->ctx);
        ERR_free_strings();
        EVP_cleanup();
    }
}

fnet_client_t *fnet_connect(char const *addr)
{
    if (!addr)
    {
        FS_ERR("Invalid address");
        return 0;
    }

    fnet_client_t *pclient = malloc(sizeof(fnet_client_t));
    if (!pclient)
    {
        FS_ERR("Unable to allocate memory for client");
        return 0;
    }
    memset(pclient, 0, sizeof *pclient);

    if (!fnet_ssl_module_init(&fnet_ssl_module))
    {
        fnet_disconnect(pclient);
        return 0;
    }

    pclient->bio = BIO_new_ssl_connect(fnet_ssl_module.ctx);
    if (!pclient->bio)
    {
        FS_ERR("The SSL connection isn't created. Error: \'%s\'", ERR_reason_error_string(ERR_get_error()));
        fnet_disconnect(pclient);
        return 0;
    }

    BIO_get_ssl(pclient->bio, &pclient->ssl);
    if (!pclient->ssl)
    {
        FS_ERR("Can't locate SSL pointer. Error: \'%s\'", ERR_reason_error_string(ERR_get_error()));
        fnet_disconnect(pclient);
        return 0;
    }

    SSL_set_mode(pclient->ssl, SSL_MODE_AUTO_RETRY);
    BIO_set_conn_hostname(pclient->bio, addr);

    if(BIO_do_connect(pclient->bio) <= 0)
    {
        FS_ERR("Error connecting to server. Error: \'%s\'", ERR_reason_error_string(ERR_get_error()));
        fnet_disconnect(pclient);
        return 0;
    }

    if(SSL_get_verify_result(pclient->ssl) != X509_V_OK)
    {
        FS_ERR("The certificate verification is failed. Error: \'%s\'", ERR_reason_error_string(ERR_get_error()));
        fnet_disconnect(pclient);
        return 0;
    }

    if (BIO_do_handshake(pclient->bio) <= 0)
    {
        FS_ERR("Error establishing SSL connection. Error: \'%s\'", ERR_reason_error_string(ERR_get_error()));
        fnet_disconnect(pclient);
        return 0;
    }

    return pclient;
}

void fnet_disconnect(fnet_client_t *pclient)
{
    if (pclient)
    {
        BIO_reset(pclient->bio);
        BIO_free_all(pclient->bio);
        fnet_ssl_module_uninit(&fnet_ssl_module);
        free(pclient);
    }
    else FS_ERR("Invalid argument");
}

fnet_server_t *fnet_bind(char const *addr, fnet_accepter_t accepter)
{
    if (!addr)
    {
        FS_ERR("Invalid address");
        return 0;
    }

    fnet_server_t *pserver = malloc(sizeof(fnet_server_t));
    if (!pserver)
    {
        FS_ERR("Unable to allocate memory for server");
        return 0;
    }
    memset(pserver, 0, sizeof *pserver);

    if (!fnet_ssl_module_init(&fnet_ssl_module))
    {
        fnet_unbind(pserver);
        return 0;
    }

    pserver->bio = BIO_new_ssl(fnet_ssl_module.ctx, 0);
    if (!pserver->bio)
    {
        FS_ERR("The SSL BIO isn't created. Error: \'%s\'", ERR_reason_error_string(ERR_get_error()));
        fnet_unbind(pserver);
        return 0;
    }

    BIO_get_ssl(pserver->bio, &pserver->ssl);
    if (!pserver->ssl)
    {
        FS_ERR("Can't locate SSL pointer. Error: \'%s\'", ERR_reason_error_string(ERR_get_error()));
        fnet_unbind(pserver);
        return 0;
    }

    SSL_set_mode(pserver->ssl, SSL_MODE_AUTO_RETRY);

    pserver->buffering_bio = BIO_new(BIO_f_buffer());
    if (!pserver->buffering_bio)
    {
        FS_ERR("The SSL buffering BIO isn't created. Error: \'%s\'", ERR_reason_error_string(ERR_get_error()));
        fnet_unbind(pserver);
        return 0;
    }

    pserver->bio = BIO_push(pserver->buffering_bio, pserver->bio);

    pserver->accept_bio = BIO_new_accept(addr);
    if (!pserver->accept_bio)
    {
        FS_ERR("The SSL BIO isn't created. Error: \'%s\'", ERR_reason_error_string(ERR_get_error()));
        fnet_unbind(pserver);
        return 0;
    }

    if (!BIO_set_accept_bios(pserver->accept_bio, pserver->bio))
    {
        FS_ERR("Unable to accept the BIO. Error: \'%s\'", ERR_reason_error_string(ERR_get_error()));
        fnet_unbind(pserver);
        return 0;
    }

    return pserver;
}

void fnet_unbind(fnet_server_t *pserver)
{
    if (pserver)
    {
        BIO_free_all(pserver->bio);
        fnet_ssl_module_uninit(&fnet_ssl_module);
        free(pserver);
    }
    else FS_ERR("Invalid argument");
}

fnet_client_t *fnet_accept(fnet_server_t *pserver)
{
    if (!pserver)
    {
        FS_ERR("Invalid argument");
        return 0;
    }

    // Setup accept BIO
    if(BIO_do_accept(pserver->accept_bio) <= 0)
    {
        FS_ERR("Error setting up accept BIO. Error: \'%s\'", ERR_reason_error_string(ERR_get_error()));
        return 0;
    }

    // accept BIO
    if(BIO_do_accept(pserver->accept_bio) <= 0)
    {
        FS_ERR("Error in connection accept. Error: \'%s\'", ERR_reason_error_string(ERR_get_error()));
        return 0;
    }

    BIO *cli_bio = BIO_pop(pserver->accept_bio);
    if(BIO_do_handshake(pserver->bio) <= 0)
    {
        FS_ERR("Error in SSL handshake. Error: \'%s\'", ERR_reason_error_string(ERR_get_error()));
        return 0;
    }

    // TODO
    return 0;
}
