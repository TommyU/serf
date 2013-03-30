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

#ifndef _BUCKET_PRIVATE_H_
#define _BUCKET_PRIVATE_H_

typedef struct serf_ssl_bucket_type_t serf_ssl_bucket_type_t;

struct serf_ssl_bucket_type_t {
    void * (*decrypt_create)(serf_bucket_t *bucket,
                             serf_bucket_t *stream,
                             void *impl_ctx,
                             serf_bucket_alloc_t *allocator);

    void * (*decrypt_context_get)(serf_bucket_t *bucket);

    void * (*encrypt_create)(serf_bucket_t *bucket,
                             serf_bucket_t *stream,
                             void *impl_ctx,
                             serf_bucket_alloc_t *allocator);

    void * (*encrypt_context_get)(serf_bucket_t *bucket);

    /**
     * Allow SNI indicators to be sent to the server.
     */
    apr_status_t (*set_hostname)(void *impl_ctx, const char *hostname);

    void (*client_cert_provider_set)(void *impl_ctx,
                                     serf_ssl_need_client_cert_t callback,
                                     void *data,
                                     void *cache_pool);

    void (*client_cert_password_set)(void *impl_ctx,
                                     serf_ssl_need_cert_password_t callback,
                                     void *data,
                                     void *cache_pool);
    /**
     * Set a callback to override the default SSL server certificate validation
     * algorithm.
     */
    void (*server_cert_callback_set)(void *impl_ctx,
                                     serf_ssl_need_server_cert_t callback,
                                     void *data);
    /**
     * Set callbacks to override the default SSL server certificate validation
     * algorithm for the current certificate or the entire certificate chain.
     */
    void (*server_cert_chain_callback_set)(void *impl_ctx,
                                           serf_ssl_need_server_cert_t cert_callback,
                                           serf_ssl_server_cert_chain_cb_t cert_chain_callback,
                                           void *data);
    /**
     * Use the default root CA certificates as included with the OpenSSL library.
     */
    apr_status_t (*use_default_certificates)(void *impl_ctx);
    
    /**
     * Load a CA certificate file from a path @a file_path. If the file was loaded
     * and parsed correctly, a certificate @a cert will be created and returned.
     * This certificate object will be alloced in @a pool.
     */
    apr_status_t (*load_cert_file)(serf_ssl_certificate_t **cert,
                                   const char *file_path,
                                   apr_pool_t *pool);

    /**
     * Adds the certificate @a cert to the list of trusted certificates in
     * @a ssl_ctx that will be used for verification.
     * See also @a serf_ssl_load_cert_file.
     */
    apr_status_t (*trust_cert)(void *impl_ctx,
                               serf_ssl_certificate_t *cert);

    /**
     * Enable or disable SSL compression on a SSL session.
     * @a enabled = 1 to enable compression, 0 to disable compression.
     * Default = disabled.
     */
    apr_status_t (*use_compression)(void *impl_ctx,
                                    int enabled);
};

/* ==================================================================== */

#ifdef SERF_HAVE_OPENSSL

extern const serf_ssl_bucket_type_t serf_ssl_bucket_type_openssl;

extern const serf_bucket_type_t serf_bucket_type_openssl_encrypt;
#define SERF_BUCKET_IS_OPENSSL_ENCRYPT(b) SERF_BUCKET_CHECK((b), openssl_encrypt)

extern const serf_bucket_type_t serf_bucket_type_openssl_decrypt;
#define SERF_BUCKET_IS_OPENSSL_DECRYPT(b) SERF_BUCKET_CHECK((b), openssl_decrypt)

#endif /* SERF_HAVE_OPENSSL */

/* ==================================================================== */

#if SERF_HAVE_SECURETRANSPORT

extern const serf_ssl_bucket_type_t serf_ssl_bucket_type_securetransport;

extern const serf_bucket_type_t serf_bucket_type_sectrans_encrypt;
#define SERF_BUCKET_IS_SECTRANS_ENCRYPT(b) SERF_BUCKET_CHECK((b), sectrans_encrypt)

extern const serf_bucket_type_t serf_bucket_type_sectrans_decrypt;
#define SERF_BUCKET_IS_SECTRANS_DECRYPT(b) SERF_BUCKET_CHECK((b), sectrans_decrypt)

#endif /* SERF_HAVE_SECURETRANSPORT */

/* ==================================================================== */



#endif