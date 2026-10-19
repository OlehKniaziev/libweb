#include <errno.h>

#include "../src/http.h"
#include "../src/log.h"

int main(int ArgCount, char **Args) {
    ArgCount--;
    Args++;

    WebLogSetDestination(stderr);

    if (ArgCount != 1) {
        WEB_PANIC_FMT("Program expects exactly one argument, got %d instead", ArgCount);
    }

    web_string_view FullPath = WEB_SV_LIT(Args[0]);

    web_http_context_config Config = {0};
    web_http_context Context = {0};
    WEB_VERIFY(WebHttpContextInit(Config, &Context));

    web_string_view Host = WebStringViewChopCStr(FullPath, "/");
    if (Host.Count == 0) {
        WEB_LOG(ERROR, HTTP, "No forward slashes allowed");
        return 1;
    }

    FullPath.Count -= Host.Count;
    FullPath.Items += Host.Count;

    web_string_view RequestPath = FullPath;
    if (RequestPath.Count == 0) {
        RequestPath = WEB_SV_LIT("/");
    }

    web_http_request Req = WEB_HTTP_REQUEST_BUILD(&Context, RequestPath, HTTP_GET, WEB_SV_LIT(""));
    web_http_response Resp = {0};

    if (!WebHttpRequestSend(&Context, Host, Req, &Resp)) {
        WEB_LOG(ERROR, HTTP, "Failed to send the request");
        return 1;
    }

    WEB_LOG(INFO, HTTP, "Response received successfully");

    printf("Response status code: %d\n", Resp.Status);
    printf("Response body:\r\n\t" WEB_SV_FMT "\n", WEB_SV_ARG(Resp.Body));
}
