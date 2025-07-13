#ifndef HTTP_H_
#define HTTP_H_

#include "common.h"
#include "json.h"

#ifdef __cplusplus
    extern "C" {
#endif

// NOTE(oleh): https://datatracker.ietf.org/doc/html/rfc2616#section-5.1.1
#define ENUM_HTTP_METHODS                   \
    X(OPTIONS)                              \
    X(GET)                                  \
    X(POST)                                 \
    X(PUT)                                  \
    X(DELETE)                               \
    X(TRACE)                                \
    X(CONNECT)

typedef enum {
#define X(method) HTTP_##method,
    ENUM_HTTP_METHODS
#undef X
} web_http_method;

typedef struct {
    web_string_view Name;
    web_string_view Value;
} web_http_header;

typedef struct {
    web_http_header *Items;
    uz Count;
} web_http_headers;

#define ENUM_HTTP_VERSIONS \
    X(1_1, "HTTP/1.1")

typedef enum {
#define X(Version, String) HTTP_##Version,
ENUM_HTTP_VERSIONS
#undef X
} web_http_version;

typedef struct {
    web_http_method Method;
    web_string_view Path;
    web_http_version Version;
    web_http_headers Headers;
    web_string_view Body;
} web_http_request;

b32 WebHttpRequestParse(web_arena *Arena, web_string_view Buffer, web_http_request *Out);

#define ENUM_HTTP_RESPONSE_STATUSES                             \
    X(OK, 200, "OK")                                            \
        X(BAD_REQUEST, 400, "Bad Request")                      \
        X(FORBIDDEN, 403, "Forbidden")                      \
        X(NOT_FOUND, 404, "Not Found")                          \
        X(METHOD_NOT_ALLOWED, 405, "Method Not Allowed")        \
        X(INTERNAL_SERVER_ERROR, 500, "Internal Server Error")  \

typedef enum {
#define X(Status, Code, _Phrase) HTTP_STATUS_##Status = Code,
    ENUM_HTTP_RESPONSE_STATUSES
#undef X
} web_http_response_status;

typedef struct {
    web_arena *Arena;
    web_http_request Request;
    web_string_view Content;
} web_http_response_context;

typedef web_http_response_status (*web_http_request_handler)(web_http_response_context *);

typedef struct {
    // TODO(oleh): Probably introduce a thread pool and accepting socket fd here.
    web_arena Arena;
    web_string_view *HandlersPaths;
    web_http_request_handler *Handlers;
    uz HandlersCount;
} web_http_server;

void WebHttpServerInit(web_http_server *);

void WebHttpResponseWrite(web_http_response_context *, web_string_view);

void WebHttpServerStart(web_http_server *Server, u16 Port);
void WebHttpServerAttachHandler(web_http_server *Server, const char *Path, web_http_request_handler WebHandler);

static inline b32 WebHttpContextParseJsonBody(web_http_response_context *Ctx, web_json_value *OutValue) {
    return WebJsonParse(Ctx->Arena, Ctx->Request.Body, OutValue);
}

#ifdef __cplusplus
    }
#endif

#endif // HTTP_H_
