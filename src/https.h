#ifndef HTTPS_H_
#define HTTPS_H_

#ifdef WEB_USE_HTTPS_OPENSSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif // WEB_USE_HTTPS_OPENSSL

#ifdef __cplusplus
    extern "C" {
#endif // __cplusplus

#ifdef WEB_USE_HTTPS_OPENSSL
typedef struct {
    const char *CertificateFileName;
    const char *PrivateKeyFileName;
} web_https_openssl_provider_config;
#endif // WEB_USE_HTTPS_OPENSSL

typedef struct {
    enum {
#ifdef WEB_USE_HTTPS_OPENSSL
        WEB_HTTPS_PROVIDER_OPENSSL,
#endif // WEB_USE_HTTPS_OPENSSL
        WEB_HTTPS_PROVIDER_CUSTOM,
    } Type;

    void *Data;
} web_https_provider;

typedef struct {
    void (*Init)            (void *Data);
    sz   (*AcceptConnection)(void *Data, int Sock);
    sz   (*CloseConnection) (void *Data, int Sock);
    sz   (*Read)            (void *Data, int Sock, u8 *Buffer, uz N);
    sz   (*Write)           (void *Data, int Sock, u8 *Buffer, uz N);

    const char *(*GetErrorString)(void *Data, int Error);
} web_https_custom_provider_vtable;

typedef struct {
    web_https_custom_provider_vtable VTable;
    void *Data;
} web_https_custom_provider;

#ifdef __cplusplus
    }
#endif // __cplusplus

#endif // HTTPS_H_
