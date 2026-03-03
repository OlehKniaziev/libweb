#define WEB_USE_HTTPS_OPENSSL
#include "../src/http.h"

int main() {
    web_http_server Server = {0};
    web_https_provider Provider = {
        .Type = WEB_HTTPS_PROVIDER_OPENSSL,
    };
    web_http_server_config Config = {
        .UseHttps = 1,
        .HttpsProvider = &Provider,
    };

    WebHttpServerInit(&Server, &Config);

    return 0;
}
