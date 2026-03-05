#define WEB_USE_HTTPS_OPENSSL
#include "../src/http.h"
#include "../src/log.h"

web_http_response_status HelloHandler(web_http_response_context *Ctx) {
    WebHttpResponseWrite(Ctx, WEB_SV_LIT("Hello!"));
    return HTTP_STATUS_OK;
}

int main() {
    WebLogSetDestination(stdout);

    web_http_server Server = {0};
    web_http_server_config Config = {0};

    WEB_VERIFY(WebHttpServerInit(&Server, &Config));

    WebHttpServerAttachHandler(&Server, "/hello", HelloHandler);

    u16 Port = 6969;

    WebHttpServerStart(&Server, Port);

    return 0;
}
