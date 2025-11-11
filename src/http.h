#ifndef HTTP_H_
#define HTTP_H_

#include "common.h"
#include "json.h"
#include "threadpool.h"

#ifdef __cplusplus
    extern "C" {
#endif

// NOTE(oleh): https://datatracker.ietf.org/doc/html/rfc2616#section-5.1.1
#define WEB_ENUM_HTTP_METHODS               \
    X(OPTIONS)                              \
    X(GET)                                  \
    X(POST)                                 \
    X(PUT)                                  \
    X(DELETE)                               \
    X(TRACE)                                \
    X(CONNECT)

typedef enum {
#define X(method) HTTP_##method,
    WEB_ENUM_HTTP_METHODS
#undef X
} web_http_method;

typedef struct {
    web_string_view Name;
    web_string_view Value;
} web_http_header;

typedef struct {
    web_http_header *Items;
    uz Count;
    uz Capacity;
} web_http_headers;

#define WEB_ENUM_HTTP_VERSIONS \
    X(1_1, "HTTP/1.1")

typedef enum {
#define X(Version, String) HTTP_##Version,
WEB_ENUM_HTTP_VERSIONS
#undef X
} web_http_version;

#define WEB_ENUM_HTTP_RESPONSE_STATUSES                             \
    X(OK, 200, "OK")                                            \
    X(CREATED, 201, "Created") \
    X(ACCEPTED, 202, "Accepted") \
    X(NON_AUTHORITATIVE_INFORMATION, 203, "Non-Authoritative Information") \
    X(NO_CONTENT, 204, "No Content") \
    X(RESET_CONTENT, 205, "Reset Content") \
    X(PARTIAL_CONTENT, 206, "Partial Content") \
    X(MULTI_STATUS, 207, "Multi-Status") \
    X(ALREADY_REPORTED, 208, "Already Reported") \
    X(IM_USED, 226, "IM Used") \
    \
    X(MULTIPLE_CHOICES, 300, "Multiple Choices") \
    X(MOVED_PERMANENTLY, 301, "Moved Permanently") \
    X(FOUND, 302, "Found") \
    X(SEE_OTHER, 303, "See Other") \
    X(NOT_MODIFIED, 304, "Not Modified") \
    X(TEMPORARY_REDIRECT, 307, "Temporary Redirect") \
    X(PERMANENT_REDIRECT, 308, "Permanent Redirect") \
    \
    X(BAD_REQUEST, 400, "Bad Request") \
    X(UNAUTHORIZED, 401, "Unauthorized") \
    X(PAYMENT_REQUIRED, 402, "Payment Required") \
    X(FORBIDDEN, 403, "Forbidden") \
    X(NOT_FOUND, 404, "Not Found") \
    X(METHOD_NOT_ALLOWED, 405, "Method Not Allowed") \
    X(NOT_ACCEPTABLE, 406, "Not Acceptable") \
    X(PROXY_AUTHENTICATION_REQUIRED, 407, "Proxy Authentication Required") \
    X(REQUEST_TIMEOUT, 408, "Request Timeout") \
    X(CONFLICT, 409, "Conflict") \
    X(GONE, 410, "Gone") \
    X(LENGTH_REQUIRED, 411, "Length Required") \
    X(PRECONDITION_FAILED, 412, "Precondition Failed") \
    X(PAYLOAD_TOO_LARGE, 413, "Content Too Large") \
    X(URI_TOO_LONG, 414, "URI Too Long") \
    X(UNSUPPORTED_MEDIA_TYPE, 415, "Unsupported Media Type") \
    X(RANGE_NOT_SATISFIABLE, 416, "Range Not Satisfiable") \
    X(EXPECTATION_FAILED, 417, "Expectation Failed") \
    X(IM_A_TEAPOT, 418, "I'm a teapot") \
    X(MISDIRECTED_REQUEST, 421, "Misdirected Request") \
    X(UNPROCESSABLE_ENTITY, 422, "Unprocessable Entity") \
    X(LOCKED, 423, "Locked") \
    X(FAILED_DEPENDENCY, 424, "Failed Dependency") \
    X(TOO_EARLY, 425, "Too Early") \
    X(UPGRADE_REQUIRED, 426, "Upgrade Required") \
    X(PRECONDITION_REQUIRED, 428, "Precondition Required") \
    X(TOO_MANY_REQUESTS, 429, "Too Many Requests") \
    X(REQUEST_HEADER_FIELDS_TOO_LARGE, 431, "Request Header Fields Too Large") \
    X(UNAVAILABLE_FOR_LEGAL_REASONS, 451, "Unavailable For Legal Reasons") \
    \
    X(INTERNAL_SERVER_ERROR, 500, "Internal Server Error") \
    X(NOT_IMPLEMENTED, 501, "Not Implemented") \
    X(BAD_GATEWAY, 502, "Bad Gateway") \
    X(SERVICE_UNAVAILABLE, 503, "Service Unavailable") \
    X(GATEWAY_TIMEOUT, 504, "Gateway Timeout") \
    X(HTTP_VERSION_NOT_SUPPORTED, 505, "HTTP Version Not Supported") \
    X(VARIANT_ALSO_NEGOTIATES, 506, "Variant Also Negotiates") \
    X(INSUFFICIENT_STORAGE, 507, "Insufficient Storage") \
    X(LOOP_DETECTED, 508, "Loop Detected") \
    X(NOT_EXTENDED, 510, "Not Extended") \
    X(NETWORK_AUTHENTICATION_REQUIRED, 511, "Network Authentication Required")

typedef enum {
#define X(Status, Code, _Phrase) HTTP_STATUS_##Status = Code,
    WEB_ENUM_HTTP_RESPONSE_STATUSES
#undef X
} web_http_response_status;

const char *WebHttpGetResponseStatusReason(web_http_response_status);

typedef struct {
    web_http_method Method;
    web_string_view Path;
    web_http_version Version;
    web_http_headers Headers;
    web_string_view Body;
} web_http_request;

typedef struct {
    web_http_version Version;
    web_http_response_status Status;
    web_http_headers Headers;
    web_string_view Body;
} web_http_response;

b32 WebHttpRequestParse(web_arena *Arena, web_string_view Buffer, web_http_request *Out);
b32 WebHttpRequestSend(web_arena *Arena,
                       web_string_view Hostname,
                       u16 Port,
                       web_http_request Request,
                       web_http_response *Response);

b32 WebHttpResponseParse(web_arena *Arena, web_string_view Buffer, web_http_response *OutResponse);

typedef struct {
    web_arena Arena;
    web_http_request Request;
    web_http_headers ResponseHeaders;
    web_string_view Content;
} web_http_response_context;

typedef web_http_response_status (*web_http_request_handler)(web_http_response_context *);

typedef struct {
    // TODO(oleh): Probably introduce a thread pool and accepting socket fd here.
    web_arena Arena;
    web_string_view *HandlersPaths;
    web_http_request_handler *Handlers;
    uz HandlersCount;
    uz ThreadsCount;
    web_thread_pool ThreadPool;
} web_http_server;

typedef struct {
    s16 NumThreads;
} web_http_server_config;

b32 WebHttpServerInit(web_http_server *, web_http_server_config *);

void WebHttpResponseWrite(web_http_response_context *, web_string_view);

void WebHttpServerStart(web_http_server *Server, u16 Port);
void WebHttpServerAttachHandler(web_http_server *Server, const char *Path, web_http_request_handler WebHandler);

static inline b32 WebHttpContextParseJsonBody(web_http_response_context *Ctx, web_json_value *OutValue) {
    return WebJsonParse(&Ctx->Arena, Ctx->Request.Body, OutValue);
}

void WebHttpContextAddHeader(web_http_response_context *Ctx, web_string_view Name, web_string_view Value);

#ifdef __cplusplus
    }
#endif

#endif // HTTP_H_
