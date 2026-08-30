#include "https.h"

sz OpenSSLSessionRead(void *Ptr, u8 *Buf, uz BufCount) {
    SSL *Ssl = (SSL *) Ptr;
    return SSL_read(Ssl, Buf, BufCount);
}

sz OpenSSLSessionWrite(void *Ptr, u8 *Buf, uz BufCount) {
    SSL *Ssl = (SSL *) Ptr;
    return SSL_write(Ssl, Buf, BufCount);
}

sz OpenSSLSessionClose(void *Ptr) {
    SSL *Ssl = (SSL *) Ptr;
    return SSL_shutdown(Ssl);
}

void WebHttpsProviderInitOpenSSL(web_https_openssl_provider_config *Config, web_https_provider *Provider) {
    Provider->Type = WEB_HTTPS_PROVIDER_OPENSSL;
    Provider->Data = Config;
}
