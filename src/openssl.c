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
