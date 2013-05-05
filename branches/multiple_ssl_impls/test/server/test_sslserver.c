/* Copyright 2013 Justin Erenkrantz and Greg Stein
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "serf.h"
#include "test_server.h"

#include "serf_private.h"

#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#define TEST_VERBOSE 0

static int init_done = 0;

typedef struct ssl_context_t {
    int handshake_done;

    SSL_CTX* ctx;
    SSL* ssl;
    BIO *bio;

} ssl_context_t;

int pem_passwd_cb(char *buf, int size, int rwflag, void *userdata)
{
    strncpy(buf, "serftest", size);
    buf[size - 1] = '\0';
    return strlen(buf);
}

static int bio_apr_socket_create(BIO *bio)
{
    bio->shutdown = 1;
    bio->init = 1;
    bio->num = -1;
    bio->ptr = NULL;

    return 1;
}

static int bio_apr_socket_destroy(BIO *bio)
{
    /* Did we already free this? */
    if (bio == NULL) {
        return 0;
    }

    return 1;
}

static long bio_apr_socket_ctrl(BIO *bio, int cmd, long num, void *ptr)
{
    long ret = 1;

    switch (cmd) {
        default:
            /* abort(); */
            break;
        case BIO_CTRL_FLUSH:
            /* At this point we can't force a flush. */
            break;
        case BIO_CTRL_PUSH:
        case BIO_CTRL_POP:
            ret = 0;
            break;
    }
    return ret;
}

/* Returns the amount read. */
static int bio_apr_socket_read(BIO *bio, char *in, int inlen)
{
    apr_size_t len = inlen;
    serv_ctx_t *serv_ctx = bio->ptr;
    apr_status_t status;

    BIO_clear_retry_flags(bio);

    status = apr_socket_recv(serv_ctx->client_sock, in, &len);
    if (status == APR_EAGAIN) {
        BIO_set_retry_read(bio);
        if (len == 0)
            return -1;

    }

    if (SERF_BUCKET_READ_ERROR(status))
        return -1;

    serf__log(TEST_VERBOSE, __FILE__, "Read %d bytes from socket with status %d.\n",
              len, status);

    return len;
}

/* Returns the amount written. */
static int bio_apr_socket_write(BIO *bio, const char *in, int inlen)
{
    apr_size_t len = inlen;
    serv_ctx_t *serv_ctx = bio->ptr;

    apr_status_t status = apr_socket_send(serv_ctx->client_sock, in, &len);

    if (SERF_BUCKET_READ_ERROR(status))
        return -1;

    serf__log(TEST_VERBOSE, __FILE__, "Wrote %d of %d bytes to socket.\n",
              len, inlen);

    return len;
}


static BIO_METHOD bio_apr_socket_method = {
    BIO_TYPE_SOCKET,
    "APR sockets",
    bio_apr_socket_write,
    bio_apr_socket_read,
    NULL,                        /* Is this called? */
    NULL,                        /* Is this called? */
    bio_apr_socket_ctrl,
    bio_apr_socket_create,
    bio_apr_socket_destroy,
#ifdef OPENSSL_VERSION_NUMBER
    NULL /* sslc does not have the callback_ctrl field */
#endif
};

static int validate_client_certificate(int preverify_ok, X509_STORE_CTX *ctx)
{
    return preverify_ok;
}

apr_status_t init_ssl_context(serv_ctx_t *serv_ctx,
                              const char *keyfile,
                              const char **certfiles,
                              const char *client_cn)
{
    ssl_context_t *ssl_ctx = apr_pcalloc(serv_ctx->pool, sizeof(*ssl_ctx));
    serv_ctx->ssl_ctx = ssl_ctx;
    serv_ctx->client_cn = client_cn;

    /* Init OpenSSL globally */
    if (!init_done)
    {
        CRYPTO_malloc_init();
        ERR_load_crypto_strings();
        SSL_load_error_strings();
        SSL_library_init();
        OpenSSL_add_all_algorithms();
        init_done = 1;
    }

    /* Init this connection */
    if (!ssl_ctx->ctx) {
        X509_STORE *store;
        const char *certfile;
        int i;

        ssl_ctx->ctx = SSL_CTX_new(SSLv23_server_method());
        SSL_CTX_set_cipher_list(ssl_ctx->ctx, "ALL");
        SSL_CTX_set_default_passwd_cb(ssl_ctx->ctx, pem_passwd_cb);

        ssl_ctx->ssl = SSL_new(ssl_ctx->ctx);
        SSL_use_PrivateKey_file(ssl_ctx->ssl, keyfile, SSL_FILETYPE_PEM);

        /* Set server certificate, add ca certificates if provided. */
        certfile = certfiles[0];
        SSL_use_certificate_file(ssl_ctx->ssl, certfile, SSL_FILETYPE_PEM);

        i = 1;
        certfile = certfiles[i++];
        store = SSL_CTX_get_cert_store(ssl_ctx->ctx);

        while(certfile) {
            FILE *fp = fopen(certfile, "r");
            if (fp) {
                X509 *ssl_cert = PEM_read_X509(fp, NULL, NULL, NULL);
                fclose(fp);

                SSL_CTX_add_extra_chain_cert(ssl_ctx->ctx, ssl_cert);

                X509_STORE_add_cert(store, ssl_cert);
            }
            certfile = certfiles[i++];
        }

        SSL_set_verify(ssl_ctx->ssl, SSL_VERIFY_PEER,
                       validate_client_certificate);

        SSL_set_mode(ssl_ctx->ssl, SSL_MODE_ENABLE_PARTIAL_WRITE);
        ssl_ctx->bio = BIO_new(&bio_apr_socket_method);
        ssl_ctx->bio->ptr = serv_ctx;
        SSL_set_bio(ssl_ctx->ssl, ssl_ctx->bio, ssl_ctx->bio);
    }

    return APR_SUCCESS;
}

apr_status_t ssl_handshake(serv_ctx_t *serv_ctx)
{
    ssl_context_t *ssl_ctx = serv_ctx->ssl_ctx;
    int result;

    if (ssl_ctx->handshake_done)
        return APR_SUCCESS;

    /* SSL handshake */
    result = SSL_accept(ssl_ctx->ssl);
    if (result == 1) {
        X509 *peer;

        serf__log(TEST_VERBOSE, __FILE__, "Handshake successful.\n");

        /* Check client certificate */
        peer = SSL_get_peer_certificate(ssl_ctx->ssl);
        if (peer)
        {
            serf__log(TEST_VERBOSE, __FILE__, "Peer cert received.\n");
            if (SSL_get_verify_result(ssl_ctx->ssl) == X509_V_OK)
            {
                /* The client sent a certificate which verified OK */
                char buf[1024];
                int ret;
                X509_NAME *subject = X509_get_subject_name(peer);

                ret = X509_NAME_get_text_by_NID(subject,
                                                NID_commonName,
                                                buf, 1024);
                if (ret != -1 && strcmp(serv_ctx->client_cn, buf) != 0) {
                    serf__log(TEST_VERBOSE, __FILE__, "Client cert common name "
                              "\"%s\" doesn't match expected \"%s\".\n", buf,
                              serv_ctx->client_cn);
                    return APR_EGENERAL;

                }
            }
        } else {
            if (serv_ctx->client_cn) {
                serf__log(TEST_VERBOSE, __FILE__, "Client cert expected but not"
                          " received.\n");
                return APR_EGENERAL;
            }
        }

        ssl_ctx->handshake_done = 1;
    }
    else {
        int ssl_err;

        ssl_err = SSL_get_error(ssl_ctx->ssl, result);
        switch (ssl_err) {
            case SSL_ERROR_WANT_READ:
            case SSL_ERROR_WANT_WRITE:
                return APR_EAGAIN;
            default:
                serf__log(TEST_VERBOSE, __FILE__, "SSL Error %d: ", ssl_err);
                ERR_print_errors_fp(stderr);
                serf__log_nopref(TEST_VERBOSE, "\n");
                return APR_EGENERAL;
        }
    }

    return APR_EAGAIN;
}

apr_status_t
ssl_socket_write(serv_ctx_t *serv_ctx, const char *data,
                 apr_size_t *len)
{
    ssl_context_t *ssl_ctx = serv_ctx->ssl_ctx;

    int result = SSL_write(ssl_ctx->ssl, data, *len);
    if (result > 0) {
        *len = result;
        return APR_SUCCESS;
    }

    if (result == 0)
        return APR_EAGAIN;
    
    return APR_EGENERAL;
}

apr_status_t
ssl_socket_read(serv_ctx_t *serv_ctx, char *data,
                apr_size_t *len)
{
    ssl_context_t *ssl_ctx = serv_ctx->ssl_ctx;

    int result = SSL_read(ssl_ctx->ssl, data, *len);
    if (result > 0) {
        *len = result;
        return APR_SUCCESS;
    }

    return APR_EGENERAL;
}

void cleanup_ssl_context(serv_ctx_t *serv_ctx)
{
    ssl_context_t *ssl_ctx = serv_ctx->ssl_ctx;

    SSL_clear(ssl_ctx->ssl);
    SSL_CTX_free(ssl_ctx->ctx);
}