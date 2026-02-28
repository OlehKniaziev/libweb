#ifndef HTTPS_H_
#define HTTPS_H_

#ifndef WEB_USE_HTTPS
#error "Define 'WEB_USE_HTTPS' to use this file"
#endif // WEB_USE_HTTPS

#ifdef __cplusplus
    extern "C" {
#endif // __cplusplus

typedef struct {
    enum {
        WEB_HTTPS_PROVIDER_OPENSSL,
        WEB_HTTPS_PROVIDER_CUSTOM,
    } Type;

    void *Data;
} web_https_provider;

typedef struct {
    sz (*Read) (void *Data, int Sock, u8 *Buffer, uz N);
    sz (*Write)(void *Data, int Sock, u8 *Buffer, uz N);
} web_https_custom_provider_vtable;

typedef struct {
    web_https_custom_provider_vtable VTable;
    void *Data;
} web_https_custom_provider;

#ifdef __cplusplus
    }
#endif // __cplusplus

#endif // HTTPS_H_
