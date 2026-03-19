#define WEB_USE_HTTPS_OPENSSL
#include "../src/http.h"
#include "../src/log.h"

web_http_response_status RootHandler(web_http_response_context *Ctx) {
    WebHttpResponseWrite(Ctx, Ctx->Request.Body);
    return HTTP_STATUS_OK;
}

int main() {
    WebLogSetDestination(stdout);

    web_http_server Server = {0};
    web_http_server_config Config = {0};

    WEB_VERIFY(WebHttpServerInit(&Server, &Config));

    WebHttpServerAttachHandler(&Server, "/", RootHandler);

    u16 Port = 7171;

    WebHttpServerStart(&Server, Port);

    return 0;
}
