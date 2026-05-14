#include "http.h"
#include "threadpool.h"
#include "log.h"

#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>

#include <errno.h>

static const char *HttpVersionStrings[] = {
#define X(Version, String) [HTTP_##Version] = String,
WEB_ENUM_HTTP_VERSIONS
#undef X
};

static const char *HttpMethodNames[] = {
#define X(Method) [HTTP_##Method] = #Method,
WEB_ENUM_HTTP_METHODS
#undef X
};

const char *WebHttpGetResponseStatusReason(web_http_response_status Status) {
#define X(Name, Code, Reason) if (Status == HTTP_STATUS_##Name) return Reason;
WEB_ENUM_HTTP_RESPONSE_STATUSES
#undef X
    WEB_PANIC_FMT("Unknown response status %d", Status);
}

typedef struct {
    web_http_context *Context;
    web_http_server *Server;

    int Sock;
    web_https_session HttpsSession;

    web_http_request Request;
    web_http_response *Response;
    b32 StatusBit;
} worker_data;

static sz HttpsWrite(web_https_session *Sess, web_string_view ResponseString) {
    return Sess->VTable.Write(Sess->Data, ResponseString.Items, ResponseString.Count);
}

// TODO(oleh): Get the error string.
static sz HttpSend(worker_data *WorkerData, web_string_view Data) {
    if (WorkerData->Context->UseHttps) {
        return HttpsWrite(&WorkerData->HttpsSession, Data);
    } else {
        return write(WorkerData->Sock, Data.Items, Data.Count);
    }
}

static sz HttpsRead(web_https_session *Sess, u8 *Buffer, uz BufferCapacity) {
    return Sess->VTable.Read(Sess->Data, Buffer, BufferCapacity);
}

// TODO(oleh): Get the error string.
static sz HttpReceive(worker_data *WorkerData, u8 *Buffer, uz BufferCapacity) {
    if (WorkerData->Server->Context->UseHttps) {
        return HttpsRead(&WorkerData->HttpsSession,
                         Buffer,
                         BufferCapacity);
    } else {
        return read(WorkerData->Sock, Buffer, BufferCapacity);
    }
}

static void HttpHeadersFormat(web_arena *Arena, web_dynamic_string *String, web_http_headers Headers) {
    for (uz HeaderIndex = 0; HeaderIndex < Headers.Count; ++HeaderIndex) {
        web_http_header Header = Headers.Items[HeaderIndex];

        for (uz I = 0; I < Header.Name.Count; ++I) {
            WEB_ARRAY_PUSH(Arena, String, Header.Name.Items[I]);
        }

        WEB_ARRAY_PUSH(Arena, String, ':');
        WEB_ARRAY_PUSH(Arena, String, ' ');

        for (uz I = 0; I < Header.Value.Count; ++I) {
            WEB_ARRAY_PUSH(Arena, String, Header.Value.Items[I]);
        }

        WEB_ARRAY_PUSH(Arena, String, '\r');
        WEB_ARRAY_PUSH(Arena, String, '\n');
    }
}

#define WEB_HTTP_RESPONSE_MAX_SIZE (128l * 1024l * 512l)

static web_string_view HttpRequestToString(web_arena *Arena, web_http_request Request) {
    const char *VersionString = HttpVersionStrings[Request.Version];
    web_string_view VersionSv = WEB_SV_LIT(VersionString);

    const char *MethodString = HttpMethodNames[Request.Method];
    web_string_view MethodSv = WEB_SV_LIT(MethodString);

    web_dynamic_string RequestString;
    WEB_ARRAY_INIT(Arena, &RequestString);

    // Request line.
    WEB_ARRAY_EXTEND(Arena, &RequestString, &MethodSv);
    WEB_ARRAY_PUSH(Arena, &RequestString, ' ');
    WEB_ARRAY_EXTEND(Arena, &RequestString, &Request.Path);
    WEB_ARRAY_PUSH(Arena, &RequestString, ' ');
    WEB_ARRAY_EXTEND(Arena, &RequestString, &VersionSv);
    WEB_ARRAY_PUSH(Arena, &RequestString, '\r');
    WEB_ARRAY_PUSH(Arena, &RequestString, '\n');

    // Headers.
    HttpHeadersFormat(Arena, &RequestString, Request.Headers);
    WEB_ARRAY_PUSH(Arena, &RequestString, '\r');
    WEB_ARRAY_PUSH(Arena, &RequestString, '\n');

    // Body.
    WEB_ARRAY_EXTEND(Arena, &RequestString, &Request.Body);


    return (web_string_view){.Items = RequestString.Items, .Count = RequestString.Count};
}

#ifdef WEB_USE_HTTPS_OPENSSL
static SSL *PrepareOpenSSLSession(web_https_provider *Provider, int Sock, web_https_session *Sess) {
    WEB_ASSERT(Provider->Type == WEB_HTTPS_PROVIDER_OPENSSL);

    static web_https_session_vtable OpenSSLSessionVTable = {
        .Read = OpenSSLSessionRead,
        .Write = OpenSSLSessionWrite,
        .Close = OpenSSLSessionClose,
    };

    SSL_CTX *SslCtx = (SSL_CTX *) Provider->Data;
    SSL *Ssl = SSL_new(SslCtx);
    SSL_set_fd(Ssl, Sock);
    Sess->Data = Ssl;
    Sess->VTable = OpenSSLSessionVTable;

    return Ssl;
}
#endif // WEB_USE_HTTPS_OPENSSL

static int HttpsConnect(web_https_provider *Provider, int Sock, web_https_session *Sess) {
    switch (Provider->Type) {
    WEB_CASE_PROVIDER_OPENSSL({
        SSL *Ssl = PrepareOpenSSLSession(Provider, Sock, Sess);
        return SSL_connect(Ssl);
    });
    case WEB_HTTPS_PROVIDER_CUSTOM: {
        web_https_custom_provider *Custom = (web_https_custom_provider *) Provider->Data;
        return Custom->VTable.Connect(Custom->Data, Sock, Sess);
    }
    }

    WEB_UNREACHABLE();
}

static int HttpsAcceptConnection(web_https_provider *Provider, int Sock, web_https_session *Sess) {
    switch (Provider->Type) {
    WEB_CASE_PROVIDER_OPENSSL({
        SSL *Ssl = PrepareOpenSSLSession(Provider, Sock, Sess);
        return SSL_accept(Ssl);
    });
    case WEB_HTTPS_PROVIDER_CUSTOM: {
        web_https_custom_provider *Custom = (web_https_custom_provider *) Provider->Data;
        return Custom->VTable.AcceptConnection(Custom->Data, Sock, Sess);
    }
    }

    WEB_UNREACHABLE();
}

// NOTE(oleh): Not sure if it's fine to use temp here for most of the stuff as the temp arena is kinda
// expected to be small.
static void HttpRequestSendProc(void *DataPtr) {
    worker_data *WorkerData = (worker_data *) DataPtr;

    b32 Result = 1;

    web_arena *Temp = WebGetTempArena();

    web_string_view RequestString = HttpRequestToString(Temp, WorkerData->Request);

    int Status = HttpSend(WorkerData, RequestString);
    if (Status == -1) {
        Result = 0;
        goto End;
    }

    uz ResponseArenaAvailableMemory = WebArenaAvail(&WorkerData->Context->Arena);
    // FIXME(oleh): Replace the magic number.
    uz ResponseBufferCount = WEB_MIN(ResponseArenaAvailableMemory / 16, WEB_HTTP_RESPONSE_MAX_SIZE);
    u8 *ResponseBuffer = WebArenaPush(Temp, ResponseBufferCount);

    Status = read(WorkerData->Sock, ResponseBuffer, ResponseBufferCount);
    if (Status == -1) {
        Result = 0;
        goto End;
    }

    web_string_view ResponseSv = {
        .Items = ResponseBuffer,
        .Count = Status,
    };

    if (!WebHttpResponseParse(Temp, ResponseSv, WorkerData->Response)) {
        Result = 0;
        goto End;
    }

End:
    WorkerData->StatusBit = Result;
}

b32 WebHttpRequestSend(web_http_context *Context,
                       web_string_view Hostname,
                       u16 Port,
                       web_http_request Request,
                       web_http_response *Response) {
    web_arena *Temp = WebGetTempArena();
    b32 Result = 1;

    struct addrinfo Hints = {0};
    struct addrinfo* ServerAddr = NULL;

    Hints.ai_family = AF_INET;
    Hints.ai_socktype = SOCK_STREAM;
    Hints.ai_flags = AI_PASSIVE;

    const char *HostnameCStr = WebStringViewCloneCStr(Temp, Hostname);

    char PortCStr[6] = {0};
    sprintf(PortCStr, "%hu", Port);

    sz Status = getaddrinfo(HostnameCStr, PortCStr, &Hints, &ServerAddr);
    if (Status != 0) {
        // FIXME(oleh): Report an error.
        Result = 0;
        goto End;
    }

    int Sock = socket(ServerAddr->ai_family, ServerAddr->ai_socktype, 0);
    if (Sock == -1) {
        // FIXME(oleh): Report an error.
        Result = 0;
        goto End;
    }

    Status = connect(Sock, ServerAddr->ai_addr, ServerAddr->ai_addrlen);
    if (Status != 0) {
        Result = 0;
        goto End;
    }

    web_https_session HttpsSession = {0};

    if (Context->UseHttps) {
        Status = HttpsConnect(Context->HttpsProvider, Sock, &HttpsSession);

        if (Status <= 0) WEB_TODO();
    }

    worker_data *WorkerData = WebSyncPoolAlloc(&Context->WorkerPool);

    WorkerData->Context = Context;
    WorkerData->Sock = Sock;
    WorkerData->HttpsSession = HttpsSession;
    WorkerData->Request = Request;
    WorkerData->Response = Response;

    web_thread_pool_task *Task = WEB_ARENA_NEW(Temp, web_thread_pool_task);
    WebThreadPoolTaskInit(Task, HttpRequestSendProc, WorkerData);

    WebThreadPoolScheduleTask(&Context->ThreadPool, Task);

    WebThreadPoolTaskWaitUntilCompletion(Task);

    Result = WorkerData->StatusBit;

End:
    if (ServerAddr != NULL) {
        freeaddrinfo(ServerAddr);
    }

    return Result;
}

static sz HttpRequestParseHeader(u8 *Buffer,
                                 uz BufferCount,
                                 web_http_header *Header) {
    uz I = 0;

    for (; I < BufferCount; ++I) {
        if (Buffer[I] == ':') break;
    }

    if (I >= BufferCount) {
        return -1;
    }

    Header->Name = (web_string_view) {.Items = Buffer, .Count = I};

    uz HeaderValueStart = I + 2;

    for (I = HeaderValueStart; I < BufferCount; ++I) {
        if (Buffer[I] == '\r') break;
    }

    if (I >= BufferCount) {
        return -1;
    }

    if (BufferCount - I <= 1) {
        return -1;
    }

    u8 NewlineChar = Buffer[I + 1];
    if (NewlineChar != '\n') {
        return -1;
    }

    Header->Value = (web_string_view) {.Items = Buffer + HeaderValueStart, .Count = I - HeaderValueStart};

    return I + 2;
}

// TODO(oleh): This should be more strict, i.e. now it does not check that the ':' is before the CRLF.
static b32 HttpHeadersParse(web_arena *Arena,
                            web_string_view Buffer,
                            uz *Offset,
                            web_http_headers *Headers) {
    b32 Result = 1;
    uz I;

    for (I = *Offset; I < Buffer.Count; ) {
        if (Buffer.Items[I] == '\r' && Buffer.Count - I > 1 && Buffer.Items[I + 1] == '\n') goto Success;

        web_http_header Header = {0};
        sz N = HttpRequestParseHeader(Buffer.Items + I, Buffer.Count - I, &Header);
        if (N == -1) {
            return 0;
        }

        I += N;

        WEB_ARRAY_PUSH(Arena, Headers, Header);
    }

    Result = 0;

Success:
    *Offset = I + 2;

    return Result;
}

b32 WebHttpResponseParse(web_arena *Arena, web_string_view Buffer, web_http_response *OutResponse) {
    // 1. Response line. (https://datatracker.ietf.org/doc/html/rfc2616#section-6.1)
    // 1.1. Version.

    uz I;
    for (I = 0; I < Buffer.Count; ++I) {
        if (Buffer.Items[I] == ' ') break;
    }

    if (I >= Buffer.Count) return 0;

    web_http_version HttpVersion;
    web_string_view HttpVersionSv = {.Items = Buffer.Items, .Count = I};
#define X(Version, VersionString) if (WebStringViewEqualCStr(HttpVersionSv, VersionString)) { \
    HttpVersion = HTTP_##Version; \
    goto ResponseVersionSuccess; \
    }
WEB_ENUM_HTTP_VERSIONS
#undef X

    // Failed to parse the version.
    return 0;

ResponseVersionSuccess: ;
    uz StatusCodeStart = I + 1;

    for (I = StatusCodeStart; I < Buffer.Count; ++I) {
        if (Buffer.Items[I] == ' ') break;
    }

    if (I >= Buffer.Count) return 0;

    s64 StatusCodeNum;
    web_string_view StatusCodeSv = {.Items = Buffer.Items + StatusCodeStart, .Count = I - StatusCodeStart};
    if (!WebParseS64(StatusCodeSv, &StatusCodeNum)) return 0;

    web_http_response_status StatusCode;

    uz ReasonStart = I + 1;
    for (I = ReasonStart; I < Buffer.Count; ++I) {
        if (Buffer.Items[I] == '\r') break;
    }

    web_string_view ReasonPhrase = {.Items = Buffer.Items + ReasonStart, .Count = I - ReasonStart};

    ++I;
    if (I >= Buffer.Count) return 0;

    if (Buffer.Items[I] != '\n') return 0;

    // NOTE(oleh): This reason phrase is not required to be matching the status code, so maybe
    // we should just not check if it matches?
#define X(Name, Code, Reason) if (StatusCodeNum == Code && WebStringViewEqualCStr(ReasonPhrase, Reason)) { StatusCode = HTTP_STATUS_##Name; goto ResponseStatusSuccess; }
WEB_ENUM_HTTP_RESPONSE_STATUSES
#undef X

    // Failed to parse response status code.
    return 0;
ResponseStatusSuccess: ;
    ++I;

    web_http_headers Headers;
    WEB_ARRAY_INIT(Arena, &Headers);

    if (!HttpHeadersParse(Arena, Buffer, &I, &Headers)) return 0;

    web_string_view ResponseBody = {.Items = Buffer.Items + I, .Count = Buffer.Count - I};

    OutResponse->Version = HttpVersion;
    OutResponse->Body = ResponseBody;
    OutResponse->Status = StatusCode;
    OutResponse->Headers = Headers;
    return 1;
}

static sz HttpRequestParseRequestLine(u8 *Buffer,
                                      uz BufferCount,
                                      web_http_method *Method,
                                      web_string_view *Path,
                                      web_http_version *Version) {
    uz I;
    for (I = 0; I < BufferCount; ++I) {
        if (Buffer[I] == ' ') break;
    }

    if (I >= BufferCount) {
        return -1;
    }

    web_http_method RequestMethod;
    web_string_view RequestMethodSv = {.Items = Buffer, .Count = I};
#define X(Method) if (WebStringViewEqualCStr(RequestMethodSv, #Method)) {  \
        RequestMethod = HTTP_##Method;                                  \
        goto RequestMethodSuccess;                                      \
    }

    WEB_ENUM_HTTP_METHODS

#undef X

    // Failed to parse the HTTP method.
    return -1;

 RequestMethodSuccess: ;
    // 1.2. Request URI. (https://datatracker.ietf.org/doc/html/rfc2616#section-5.1.2)
    // FIXME(oleh): Actually parse URI's.

    uz PathStart = I + 1;

    for (I = PathStart; I < BufferCount; ++I) {
        if (Buffer[I] == ' ') break;
    }

    if (I >= BufferCount) {
        return -1;
    }

    web_string_view RequestPath = {.Items = Buffer + PathStart, .Count = I - PathStart};

    // 1.3. HTTP version.

    uz VersionStart = I + 1;

    for (I = VersionStart; I < BufferCount; ++I) {
        if (Buffer[I] == '\r') break;
    }

    if (I >= BufferCount) {
        return -1;
    }

    web_http_version RequestVersion;
    web_string_view VersionSv = {.Items = Buffer + VersionStart, .Count = I - VersionStart};

#define X(Version, String) if (WebStringViewEqualCStr(VersionSv, String)) { \
        RequestVersion = HTTP_##Version;                                \
        goto RequestVersionSuccess;                                     \
    }

    WEB_ENUM_HTTP_VERSIONS
#undef X

    // NOTE(oleh): Failed to recognize the HTTP version.
    return -1;
 RequestVersionSuccess:
    // 1.4. CRLF.

    if (BufferCount - I <= 1) {
        return -1;
    }

    u8 NewlineChar = Buffer[I + 1];
    if (NewlineChar != '\n') {
        return -1;
    }

    *Method = RequestMethod;
    *Path = RequestPath;
    *Version = RequestVersion;

    return I + 2;
}

b32 WebHttpRequestParse(web_arena *Arena, web_string_view Buffer, web_http_request *OutRequest, web_string_view *Error) {
    // 1. Request line. (https://datatracker.ietf.org/doc/html/rfc2616#section-5.1)
    // 1.1. Method. (https://datatracker.ietf.org/doc/html/rfc2616#section-5.1.1)
    sz N = HttpRequestParseRequestLine(Buffer.Items,
                                       0,
                                       &OutRequest->Method,
                                       &OutRequest->Path,
                                       &OutRequest->Version);

    if (N == -1) {
        *Error = WEB_SV_LIT("Could not parse the request line");
        return 0;
    }

    Buffer.Items += N;
    Buffer.Count -= N;

    // 2. Headers. (https://datatracker.ietf.org/doc/html/rfc2616#section-5.3)
    // FIXME(oleh): Actually parse the headers according to their specification.

    web_http_headers Headers;
    WEB_ARRAY_INIT(Arena, &Headers);

    uz I = 0;

    N = HttpHeadersParse(Arena, Buffer, &I, &Headers);
    if (N == -1) {
        *Error = WebArenaFormat(Arena,
                                "could not parse the request headers: " WEB_SV_FMT,
                                WEB_SV_ARG(*Error));
        return 0;
    }

    I += N;

    WEB_ASSERT(I <= Buffer.Count);

    // 3. Message body. (https://datatracker.ietf.org/doc/html/rfc2616#section-4.3)
    web_string_view RequestBody = {.Items = Buffer.Items + I, .Count = Buffer.Count - I};

    OutRequest->Headers = Headers;
    OutRequest->Body = RequestBody;

    return 1;
}

static const char *GetHttpResponseStatusReasonPhrase(web_http_response_status Status) {
    switch (Status) {
#define X(Status, _Code, Phrase) case HTTP_STATUS_##Status: return Phrase;
        WEB_ENUM_HTTP_RESPONSE_STATUSES
#undef X
    default: WEB_UNREACHABLE();
    }
}

#define TCP_BACKLOG_SIZE 256

// NOTE(oleh): This is bad.
#define DEFAULT_REQUEST_ARENA_CAPACITY (4ll * 1024ll * 1024ll * 1024ll)

typedef enum {
    PARSE_STATE_REQUEST_LINE,
    PARSE_STATE_HEADERS,
    PARSE_STATE_BODY,
} request_parse_state;

#define INITIAL_PARSE_BUFFER_CAPACITY 4096

static b32 HttpRequestParseStreaming(worker_data *WorkerData,
                                     web_arena *Arena,
                                     web_http_request *Request) {
    sz WriteOffset = 0;
    sz ParseOffset = 0;
    sz N = 0;

    sz BufferSize = 0;
    sz BufferCapacity = INITIAL_PARSE_BUFFER_CAPACITY;
    u8 *Buffer = WebArenaPush(Arena, BufferCapacity);

    request_parse_state ParseState = PARSE_STATE_REQUEST_LINE;

    s64 ContentLength = -1;
    web_http_header Header = {0};
    web_http_headers Headers = {0};
    WEB_ARRAY_INIT(Arena, &Headers);

ReceiveLoop:
    if (BufferSize >= BufferCapacity) {
        BufferCapacity <<= 1;
        Buffer = WebArenaRealloc(Arena, Buffer, BufferSize, BufferCapacity);
    }

    N = HttpReceive(WorkerData, Buffer + WriteOffset, BufferCapacity - WriteOffset);
    if (N == -1) {
        WEB_LOG(ERROR, HTTP, "Failed to receive from a client socket");
        return 0;
    }

    WriteOffset += N;
    BufferSize += N;

    switch (ParseState) {
    case PARSE_STATE_REQUEST_LINE: goto ParseRequestLine;
    case PARSE_STATE_HEADERS:      goto ParseHeaders;
    case PARSE_STATE_BODY:         goto ParseBody;
    default:                       WEB_UNREACHABLE();
    }

ParseRequestLine:
    N = HttpRequestParseRequestLine(Buffer + ParseOffset,
                                    BufferSize - ParseOffset,
                                    &Request->Method,
                                    &Request->Path,
                                    &Request->Version);
    if (N == -1) {
        WEB_LOG(ERROR, HTTP, "Failed to parse the request line");
        return 0;
    }

    ParseOffset += N;

    ParseState = PARSE_STATE_HEADERS;

ParseHeaders:
    while (ParseOffset < BufferSize) {
        if (ParseOffset < BufferSize - 1 &&
            Buffer[ParseOffset] == '\r' && Buffer[ParseOffset + 1] == '\n') {
            ParseOffset += 2;
            ParseState = PARSE_STATE_BODY;
            break;
        }

        N = HttpRequestParseHeader(Buffer + ParseOffset, BufferSize - ParseOffset, &Header);
        if (N == -1) {
            WEB_LOG(ERROR, HTTP, "Failed to parse a request's header");
            return 0;
        }

        ParseOffset += N;

        WEB_ARRAY_PUSH(Arena, &Headers, Header);

        if (WebStringViewEqualCStr(Header.Name, "Content-Length")) {
            if (!WebParseS64(Header.Value, &ContentLength)) {
                WEB_LOG_FMT(WARN,
                            HTTP,
                            "Failed to parse received Content-Length header value as an integer; value=" WEB_SV_FMT,
                            WEB_SV_ARG(Header.Value));
                ContentLength = -1;
            }
        }
    }

    if (ParseState == PARSE_STATE_HEADERS && ParseOffset >= BufferSize) goto ReceiveLoop;

ParseBody:
    if (ContentLength >= 0) {
        if (BufferSize - ParseOffset >= ContentLength) {
            Request->Body.Items = Buffer + ParseOffset;
            Request->Body.Count = ContentLength;
            return 1;
        }

        goto ReceiveLoop;
    } else {
        // TODO(oleh): Maybe this should go back to the receive loop with some timeout checks.
        Request->Body.Items = Buffer + ParseOffset;
        Request->Body.Count = BufferSize - ParseOffset;
        return 1;
    }
}

static sz HttpsCloseConnection(web_https_session *Sess) {
    return Sess->VTable.Close(Sess->Data);
}

static void ServerWorker(void *Arg) {
    worker_data *Data = (worker_data *)Arg;

    web_http_response_context *Ctx = WebSyncPoolAlloc(&Data->Context->ResponseContextPool);

    WebArenaReset(&Ctx->Arena);
    Ctx->ResponseHeaders.Count = 0;
    WEB_ARRAY_INIT(&Ctx->Arena, &Ctx->ResponseHeaders);
    Ctx->Content = (web_string_view) {0};

    web_http_request HttpRequest;
    b32 Success = HttpRequestParseStreaming(Data, &Ctx->Arena, &HttpRequest);
    if (!Success) {
        WEB_LOG(INFO, HTTP, "Could not streaming parse HTTP request");
        goto Cleanup;
    }

    Ctx->Request = HttpRequest;

    for (uz HandlerIndex = 0; HandlerIndex < Data->Server->HandlersCount; ++HandlerIndex) {
        web_string_view HandlerPath = Data->Server->HandlersPaths[HandlerIndex];
        if (!WebStringViewEqual(HandlerPath, HttpRequest.Path)) continue;

        web_http_request_handler Handler = Data->Server->Handlers[HandlerIndex];

        web_http_response_status ResponseStatus = Handler(Ctx);

        // 1. Status line. (https://datatracker.ietf.org/doc/html/rfc2616#section-6.1)
        const char *ReasonPhrase = GetHttpResponseStatusReasonPhrase(ResponseStatus);
        const char *VersionString = HttpVersionStrings[HttpRequest.Version];

        web_dynamic_string ResponseHeadersString;
        WEB_ARRAY_INIT(&Ctx->Arena, &ResponseHeadersString);

        HttpHeadersFormat(&Ctx->Arena, &ResponseHeadersString, Ctx->ResponseHeaders);

        web_string_view ResponseString = WebArenaFormat(&Ctx->Arena,
                                                        "%s %u %s\r\nAccess-Control-Allow-Origin: *\r\n" WEB_SV_FMT "\r\n" WEB_SV_FMT,
                                                        VersionString,
                                                        ResponseStatus,
                                                        ReasonPhrase,
                                                        WEB_SV_ARG(ResponseHeadersString),
                                                        WEB_SV_ARG(Ctx->Content));

        sz NumSent = HttpSend(Data, ResponseString);
        WEB_VERIFY(NumSent > 0);

        goto Cleanup;
    }

    web_http_response_status ResponseStatus = HTTP_STATUS_NOT_FOUND;
    const char *ReasonPhrase = GetHttpResponseStatusReasonPhrase(ResponseStatus);
    const char *VersionString = HttpVersionStrings[HttpRequest.Version];

    // TODO(oleh): No handler found, just give em 404!
    web_string_view ResponseString = WebArenaFormat(&Ctx->Arena,
                                                    "%s %u %s\r\nAccess-Control-Allow-Origin: *\r\n\r\n",
                                                    VersionString,
                                                    ResponseStatus,
                                                    ReasonPhrase);

    int SendStatus = send(Data->Sock, ResponseString.Items, ResponseString.Count, 0);
    WEB_ASSERT(SendStatus != -1);

Cleanup:
    if (Data->Server->Context->UseHttps) {
        HttpsCloseConnection(&Data->HttpsSession);
    }

    close(Data->Sock);
}

static const char *HttpsGetErrorString(web_https_provider *Provider, int Error) {
    switch (Provider->Type) {
    WEB_CASE_PROVIDER_OPENSSL({
        uz SslError = ERR_get_error();
        return ERR_error_string(SslError, NULL);
    });
    case WEB_HTTPS_PROVIDER_CUSTOM: {
        web_https_custom_provider *Custom = (web_https_custom_provider *) Provider->Data;
        return Custom->VTable.GetErrorString(Custom->Data, Error);
    }
    }

    WEB_UNREACHABLE();
}

void WebHttpServerStart(web_http_server *Server, u16 Port) {
    struct addrinfo Hints = {0};
    struct addrinfo* ServerAddr;

    Hints.ai_family = AF_INET;
    Hints.ai_socktype = SOCK_STREAM;
    Hints.ai_flags = AI_PASSIVE;

    char PortString[6];
    sprintf(PortString, "%d", Port);

    int Status = getaddrinfo(NULL, PortString, &Hints, &ServerAddr);
    if (Status != 0) {
        WEB_PANIC_FMT("Call to getaddrinfo failed: %s\n", gai_strerror(Status));
    }

    int ServerSock = socket(ServerAddr->ai_family, ServerAddr->ai_socktype, 0);
    if (ServerSock == -1) {
        WEB_PANIC("call to `socket` failed");
    }

    int OptValue = 1;
    if (setsockopt(ServerSock, SOL_SOCKET, SO_REUSEADDR, &OptValue, sizeof(OptValue)) == -1) {
        WEB_PANIC("Failed to set socket options");
    }

    if (bind(ServerSock, ServerAddr->ai_addr, ServerAddr->ai_addrlen) == -1) {
        WEB_PANIC("Call to `bind` failed");
    }

    if (listen(ServerSock, TCP_BACKLOG_SIZE) == -1) {
        WEB_PANIC("Call to `listen` failed");
    }

    struct sockaddr_storage ClientAddr;
    socklen_t ClientAddrSize = sizeof(ClientAddr);

    while (1) {
        int Sock = accept(ServerSock, (struct sockaddr*)&ClientAddr, &ClientAddrSize);

        if (Sock == -1) {
            int AcceptError = errno;
            close(ServerSock);
            WEB_PANIC_FMT("Could not accept a new connection: %s", strerror(AcceptError));
        }

        web_https_session HttpsSession = {0};

        if (Server->Context->UseHttps) {
            int Status = HttpsAcceptConnection(Server->Context->HttpsProvider, Sock, &HttpsSession);

            if (Status < 0) {
                const char *ErrorString = HttpsGetErrorString(Server->Context->HttpsProvider, Status);
                (void) ErrorString;
                WEB_LOG_FATAL_FMT("Sum SSL bullshit: %s", ErrorString);
            } else if (Status == 0) {
                WEB_LOG(INFO, TLS, "Client reset TLS connection");
                close(Sock);
                continue;
            }
        }

        worker_data *WorkerData = WebSyncPoolAlloc(&Server->Context->WorkerPool);
        WorkerData->Context = Server->Context;
        WorkerData->Server = Server;
        WorkerData->Sock = Sock;
        WorkerData->HttpsSession = HttpsSession;

        web_thread_pool_task *Task = WebSyncPoolAlloc(&Server->Context->TaskPool);
        WebThreadPoolTaskInit(Task, ServerWorker, WorkerData);
        WebThreadPoolScheduleTask(&Server->Context->ThreadPool, Task);
    }
}

// NOTE(oleh): Need to make sure that we are running on a system with virtual memory.
#define HTTP_SERVER_ARENA_CAPACITY (4ll * 1024ll * 1024ll * 1024ll)

// If you need more, seek help.
#define HTTP_SERVER_MAX_HANDLERS (100)

void WebHttpServerAttachHandler(web_http_server *Server, const char *Path, web_http_request_handler Handler) {
    if (Server->HandlersCount >= HTTP_SERVER_MAX_HANDLERS)
        WEB_PANIC_FMT("Maximum amount of handlers (%d) reached!", HTTP_SERVER_MAX_HANDLERS);

    uz HandlersCount = Server->HandlersCount;
    Server->HandlersPaths[HandlersCount] = WEB_SV_LIT(Path);
    Server->Handlers[HandlersCount] = Handler;
    ++Server->HandlersCount;
}

static void HttpsInit(web_https_provider *Provider) {
    switch (Provider->Type) {
#if WEB_USE_HTTPS_OPENSSL
    case WEB_HTTPS_PROVIDER_OPENSSL: {
        WEB_VERIFY(Provider->Data != NULL);

        web_https_openssl_provider_config *Conf = (web_https_openssl_provider_config *) Provider->Data;

        SSL_load_error_strings();
        ERR_load_crypto_strings();
        OpenSSL_add_all_algorithms();

        SSL_CTX *SslCtx = SSL_CTX_new(TLS_server_method());
        WEB_VERIFY(SslCtx != NULL);

        SSL_CTX_set_ecdh_auto(SslCtx, 1);

        int Ret = SSL_CTX_use_certificate_file(SslCtx, Conf->CertificateFileName, SSL_FILETYPE_PEM);
        WEB_VERIFY(Ret > 0);

        Ret = SSL_CTX_use_PrivateKey_file(SslCtx, Conf->PrivateKeyFileName, SSL_FILETYPE_PEM);
        WEB_VERIFY(Ret > 0);

        Provider->Data = SslCtx;

        return;
    }
#endif // WEB_USE_HTTPS_OPENSSL
    case WEB_HTTPS_PROVIDER_CUSTOM: {
        web_https_custom_provider *Custom = (web_https_custom_provider *) Provider->Data;
        Custom->VTable.Init(Custom->Data);

        return;
    }
    }

    WEB_UNREACHABLE();
}

static void *NewWorkerDataPoolProc(uz *Size) {
    *Size = sizeof(worker_data);
    // FIXME(oleh): Replace this asap.
    return malloc(*Size);
}

static void *NewContextPoolProc(uz *Size) {
    *Size = sizeof(web_http_response_context);
    // FIXME(oleh): Replace this asap #2.
    web_http_response_context *ResponseContext = malloc(*Size);
    WEB_STRUCT_ZERO(ResponseContext);
    WebArenaInit(&ResponseContext->Arena, DEFAULT_REQUEST_ARENA_CAPACITY);
    return ResponseContext;
}

static void *NewTaskProc(uz *Size) {
    web_thread_pool_task *Task;
    *Size = sizeof(*Task);
    // FIXME(oleh): Replace this asap #3.
    Task = malloc(*Size);
    WEB_STRUCT_ZERO(Task);
    return Task;
}

b32 WebHttpContextInit(web_http_context_config *Config, web_http_context *Context) {
    WebSyncPoolInit(&Context->WorkerPool, NewWorkerDataPoolProc);
    WebSyncPoolInit(&Context->TaskPool, NewTaskProc);
    WebSyncPoolInit(&Context->ResponseContextPool, NewContextPoolProc);

    uz NumThreads = Config->NumThreads || 1;
    web_thread_pool_config ThreadPoolConfig = {.NumThreads = NumThreads};
    return WebThreadPoolInit(&Context->ThreadPool, &Context->Arena, &ThreadPoolConfig);
}

b32 WebHttpServerInit(web_http_context *Context, web_http_server *Server) {
    if (Context->UseHttps) {
        WEB_VERIFY(Context->HttpsProvider != NULL);

        HttpsInit(Context->HttpsProvider);
    }

    WebArenaInit(&Server->Arena, HTTP_SERVER_ARENA_CAPACITY);

    Server->Handlers = WebArenaPush(&Server->Arena, sizeof(*Server->Handlers) * HTTP_SERVER_MAX_HANDLERS);
    Server->HandlersPaths = WebArenaPush(&Server->Arena, sizeof(*Server->HandlersPaths) * HTTP_SERVER_MAX_HANDLERS);

    Server->HandlersCount = 0;

    return 1;
}

void WebHttpContextAddHeader(web_http_response_context *Ctx, web_string_view Name, web_string_view Value) {
    web_http_header Header = {
        .Name = Name,
        .Value = Value,
    };
    WEB_ARRAY_PUSH(&Ctx->Arena, &Ctx->ResponseHeaders, Header);
}

void WebHttpResponseWrite(web_http_response_context *Ctx, web_string_view Response) {
    Ctx->Content = Response;
}
