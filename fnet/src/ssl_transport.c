#include "ssl_transport.h"
#include "config.h"
#include "socket.h"
#include <futils/log.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <stdbool.h>
#include <pthread.h>
#include <assert.h>

fnet_socket_t fnet_tcp_client_socket(fnet_tcp_client_t const *);

struct fnet_ssl_client
{
    pthread_mutex_t    mutex;
    SSL               *ssl;
    fnet_tcp_client_t *tcp_client;
};

struct fnet_ssl_server
{
    fnet_tcp_server_t  *tcp_server;
    fnet_ssl_accepter_t accepter;
    void               *param;
};

typedef struct
{
    volatile int ref_count;
    SSL_CTX *ctx;
} fnet_ssl_module_t;

static fnet_ssl_module_t fnet_ssl_module = { 0 };

static int fnet_pem_password_cb(char *buf, int size, int rwflag, void *param)
{
    (void)param;
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
        ERR_load_SSL_strings();
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
            SSL_CTX_set_ecdh_auto(pmodule->ctx, 1);
            SSL_CTX_set_default_passwd_cb(pmodule->ctx, fnet_pem_password_cb);
            SSL_CTX_set_verify(pmodule->ctx, SSL_VERIFY_PEER | SSL_VERIFY_CLIENT_ONCE, 0);

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

fnet_ssl_client_t *fnet_ssl_connect(char const *addr)
{
    if (!addr)
    {
        FS_ERR("Invalid address");
        return 0;
    }

    fnet_ssl_client_t *pclient = malloc(sizeof(fnet_ssl_client_t));
    if (!pclient)
    {
        FS_ERR("Unable to allocate memory for client");
        return 0;
    }
    memset(pclient, 0, sizeof *pclient);

    static pthread_mutex_t mutex_initializer = PTHREAD_MUTEX_INITIALIZER;
    pclient->mutex = mutex_initializer;

    if (!fnet_ssl_module_init(&fnet_ssl_module))
    {
        fnet_ssl_disconnect(pclient);
        return 0;
    }

    pclient->ssl = SSL_new(fnet_ssl_module.ctx);
    if (!pclient->ssl)
    {
        FS_ERR("Can't locate SSL pointer. Error: \'%s\'", ERR_reason_error_string(ERR_get_error()));
        fnet_ssl_disconnect(pclient);
        return 0;
    }

    pclient->tcp_client = fnet_tcp_connect(addr);
    if (!pclient->tcp_client)
    {
        FS_ERR("The SSL connection isn't created.");
        fnet_ssl_disconnect(pclient);
        return 0;
    }

    if (!SSL_set_fd(pclient->ssl, fnet_tcp_client_socket(pclient->tcp_client)))
    {
        FS_ERR("Unable to use the socket for SSL connection. Error: \'%s\'", ERR_reason_error_string(ERR_get_error()));
        fnet_ssl_disconnect(pclient);
        return 0;
    }

    SSL_set_mode(pclient->ssl, SSL_MODE_AUTO_RETRY);

    if (!SSL_connect(pclient->ssl))
    {
        FS_ERR("Error connecting to server. Error: \'%s\'", ERR_reason_error_string(ERR_get_error()));
        fnet_ssl_disconnect(pclient);
        return 0;
    }

    X509 *cert = SSL_get_peer_certificate(pclient->ssl);
    if(cert) X509_free(cert);
    if (!cert)
    {
        FS_ERR("The certificate verification is failed. Error: \'%s\'", ERR_reason_error_string(ERR_get_error()));
        fnet_ssl_disconnect(pclient);
        return 0;
    }

    if(SSL_get_verify_result(pclient->ssl) != X509_V_OK)
    {
        FS_ERR("The certificate verification is failed. Error: \'%s\'", ERR_reason_error_string(ERR_get_error()));
        fnet_ssl_disconnect(pclient);
        return 0;
    }

    return pclient;
}

void fnet_ssl_disconnect(fnet_ssl_client_t *pclient)
{
    if (pclient)
    {
        if (pclient->tcp_client)
            fnet_tcp_disconnect(pclient->tcp_client);
        SSL_free(pclient->ssl);
        fnet_ssl_module_uninit(&fnet_ssl_module);
        free(pclient);
    }
    else FS_ERR("Invalid argument");
}

static void fnet_tcp_clients_accepter(fnet_tcp_server_t const *tcp_server, fnet_tcp_client_t *tcp_client)
{
    if (tcp_client)
    {
        if (!fnet_ssl_module_init(&fnet_ssl_module))
            return;

        fnet_ssl_client_t *pclient = 0;

        do
        {
            fnet_ssl_client_t *pclient = malloc(sizeof(fnet_ssl_client_t));
            if (!pclient)
            {
                FS_ERR("Unable to allocate memory for client");
                break;
            }
            memset(pclient, 0, sizeof *pclient);

            static pthread_mutex_t mutex_initializer = PTHREAD_MUTEX_INITIALIZER;
            pclient->mutex = mutex_initializer;

            pclient->tcp_client = tcp_client;

            pclient->ssl = SSL_new(fnet_ssl_module.ctx);
            if (!pclient->ssl)
            {
                FS_ERR("Can't locate SSL pointer. Error: \'%s\'", ERR_reason_error_string(ERR_get_error()));
                break;
            }

            if (!SSL_set_fd(pclient->ssl, fnet_tcp_client_socket(tcp_client)))
            {
                FS_ERR("Unable to use the socket for SSL connection. Error: \'%s\'", ERR_reason_error_string(ERR_get_error()));
                break;
            }

            if (SSL_accept(pclient->ssl) <= 0)
            {
                FS_ERR("Error in connection accept. Error: \'%s\'", ERR_reason_error_string(ERR_get_error()));
                break;
            }

            X509 *cert = SSL_get_peer_certificate(pclient->ssl);
            if(cert) X509_free(cert);
            if (!cert)
            {
                FS_ERR("The certificate verification is failed. Error: \'%s\'", ERR_reason_error_string(ERR_get_error()));
                break;
            }

            if(SSL_get_verify_result(pclient->ssl) != X509_V_OK)
            {
                FS_ERR("The certificate verification is failed. Error: \'%s\'", ERR_reason_error_string(ERR_get_error()));
                break;
            }

            fnet_ssl_server_t *pserver = (fnet_ssl_server_t *)fnet_tcp_server_get_param(tcp_server);
            pserver->accepter(pserver, pclient);
            pclient = 0;
        }
        while(0);

        if (pclient)
            fnet_ssl_disconnect(pclient);
    }
}

fnet_ssl_server_t *fnet_ssl_bind(char const *addr, fnet_ssl_accepter_t accepter)
{
    if (!addr || !accepter)
    {
        FS_ERR("Invalid address");
        return 0;
    }

    fnet_ssl_server_t *pserver = malloc(sizeof(fnet_ssl_server_t));
    if (!pserver)
    {
        FS_ERR("Unable to allocate memory for server");
        return 0;
    }
    memset(pserver, 0, sizeof *pserver);
    pserver->accepter = accepter;

    if (!fnet_ssl_module_init(&fnet_ssl_module))
    {
        free(pserver);
        return 0;
    }

    pserver->tcp_server = fnet_tcp_bind(addr, fnet_tcp_clients_accepter);
    if (!pserver->tcp_server)
    {
        fnet_ssl_unbind(pserver);
        return 0;
    }

    fnet_tcp_server_set_param(pserver->tcp_server, pserver);

    return pserver;
}

void fnet_ssl_unbind(fnet_ssl_server_t *pserver)
{
    if (pserver)
    {
        if (pserver->tcp_server)
            fnet_tcp_unbind(pserver->tcp_server);
        fnet_ssl_module_uninit(&fnet_ssl_module);
        free(pserver);
    }
    else FS_ERR("Invalid argument");
}

void fnet_ssl_server_set_param(fnet_ssl_server_t *pserver, void *param)
{
    pserver->param = param;
}

void *fnet_ssl_server_get_param(fnet_ssl_server_t const *pserver)
{
    return pserver->param;
}

fnet_tcp_client_t *fnet_ssl_get_transport(fnet_ssl_client_t *pclient)
{
    if (pclient)    return pclient->tcp_client;
    else            FS_ERR("Invalid argument");
    return 0;
}

bool fnet_ssl_send(fnet_ssl_client_t *client, const void *buf, size_t len)
{
    if (!client)
    {
        FS_ERR("Invalid argument");
        return false;
    }

    if (!len)
        return true;

    int ret = SSL_write(client->ssl, buf, len);
    if (ret <= 0)
        FS_INFO("SSL_write failed: %d", SSL_get_error(client->ssl, ret));
    assert(ret > 0 ? ret == len : true);
    return ret > 0;
}

bool fnet_ssl_recv(fnet_ssl_client_t *client, void *buf, size_t len)
{
    if (!client)
    {
        FS_ERR("Invalid argument");
        return false;
    }

    int ret = SSL_read(client->ssl, buf, len);
    if (ret <= 0)
        FS_INFO("SSL_read failed: %d", SSL_get_error(client->ssl, ret));
    assert(ret > 0 ? ret == len : true);
    return ret > 0;
}

bool fnet_ssl_acquire(fnet_ssl_client_t *client)
{
    if (pthread_mutex_lock(&client->mutex))
    {
        FS_ERR("The mutex locking is failed");
        return false;
    }
    return true;
}

void fnet_ssl_release(fnet_ssl_client_t *client)
{
    (void)client;
    pthread_mutex_unlock(&client->mutex);
}
