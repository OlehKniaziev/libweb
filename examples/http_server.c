#define WEB_USE_HTTPS_OPENSSL
#include "../src/http.h"

web_http_response_status HelloHandler(web_http_response_context *Ctx) {
    WebHttpResponseWrite(Ctx, WEB_SV_LIT("Hello!"));
    return HTTP_STATUS_OK;
}

int main() {
    web_http_server Server = {0};
    web_https_provider Provider = {
        .Type = WEB_HTTPS_PROVIDER_OPENSSL,
    };
    web_http_server_config Config = {
        .UseHttps = 0,
        .HttpsProvider = &Provider,
    };

    WEB_VERIFY(WebHttpServerInit(&Server, &Config));

    WebHttpServerAttachHandler(&Server, "/hello", HelloHandler);

    u16 Port = 6969;

    WebHttpServerStart(&Server, Port);

    return 0;
}
