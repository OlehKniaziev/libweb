#ifndef HTTPS_H_
#define HTTPS_H_

#include "common.h"

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

sz OpenSSLSessionRead(void *, u8 *, uz);
sz OpenSSLSessionWrite(void *, u8 *, uz);
sz OpenSSLSessionClose(void *);

#endif // WEB_USE_HTTPS_OPENSSL

typedef struct {
    sz (*Close)(void *Data);
    sz (*Read) (void *Data, u8 *Buffer, uz N);
    sz (*Write)(void *Data, u8 *Buffer, uz N);
} web_https_session_vtable;

typedef struct {
    web_https_session_vtable VTable;
    void *Data;
} web_https_session;

typedef struct {
    enum {
#ifdef WEB_USE_HTTPS_OPENSSL
        WEB_HTTPS_PROVIDER_OPENSSL,
#endif // WEB_USE_HTTPS_OPENSSL
        WEB_HTTPS_PROVIDER_CUSTOM,
    } Type;

    void *Data;
} web_https_provider;

#ifdef WEB_USE_HTTPS_OPENSSL
#    define WEB_CASE_PROVIDER_OPENSSL(C) case WEB_HTTPS_PROVIDER_OPENSSL: { C }
#else
#    define WEB_CASE_PROVIDER_OPENSSL(C)
#endif // WEB_USE_HTTPS_OPENSSL

typedef struct {
    void (*Init)            (void *Data);
    sz   (*AcceptConnection)(void *Data, int Sock, web_https_session *Sess);

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
