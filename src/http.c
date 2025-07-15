#include "http.h"

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

b32 WebHttpRequestSend(web_arena *ResponseArena,
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

    int ServerSock = socket(ServerAddr->ai_family, ServerAddr->ai_socktype, 0);
    if (ServerSock == -1) {
        // FIXME(oleh): Report an error.
        Result = 0;
        goto End;
    }

    Status = connect(ServerSock, ServerAddr->ai_addr, ServerAddr->ai_addrlen);
    if (Status != 0) {
        Result = 0;
        goto End;
    }

    const char *VersionString = HttpVersionStrings[Request.Version];
    web_string_view VersionSv = WEB_SV_LIT(VersionString);

    const char *MethodString = HttpMethodNames[Request.Method];
    web_string_view MethodSv = WEB_SV_LIT(MethodString);

    web_dynamic_string RequestString;
    WEB_ARRAY_INIT(Temp, &RequestString);

    // Request line.
    WEB_ARRAY_EXTEND(Temp, &RequestString, &MethodSv);
    WEB_ARRAY_PUSH(Temp, &RequestString, ' ');
    WEB_ARRAY_EXTEND(Temp, &RequestString, &Request.Path);
    WEB_ARRAY_PUSH(Temp, &RequestString, ' ');
    WEB_ARRAY_EXTEND(Temp, &RequestString, &VersionSv);
    WEB_ARRAY_PUSH(Temp, &RequestString, '\r');
    WEB_ARRAY_PUSH(Temp, &RequestString, '\n');

    // Headers.
    HttpHeadersFormat(Temp, &RequestString, Request.Headers);
    WEB_ARRAY_PUSH(Temp, &RequestString, '\r');
    WEB_ARRAY_PUSH(Temp, &RequestString, '\n');

    // Body.
    WEB_ARRAY_EXTEND(Temp, &RequestString, &Request.Body);

    Status = write(ServerSock, RequestString.Items, RequestString.Count);
    if (Status == -1) {
        Result = 0;
        goto End;
    }

    uz ResponseArenaAvailableMemory = ResponseArena->Capacity - ResponseArena->Offset;
    uz ResponseBufferCount = WEB_MIN(ResponseArenaAvailableMemory / 16, WEB_HTTP_RESPONSE_MAX_SIZE);
    u8 *ResponseBuffer = WebArenaPush(ResponseArena, ResponseBufferCount);

    Status = read(ServerSock, ResponseBuffer, ResponseBufferCount);
    if (Status == -1) {
        Result = 0;
        goto End;
    }

    web_string_view ResponseSv = {
        .Items = ResponseBuffer,
        .Count = Status,
    };

    if (!WebHttpResponseParse(ResponseArena, ResponseSv, Response)) {
        Result = 0;
        goto End;
    }

End:
    if (ServerAddr != NULL) {
        freeaddrinfo(ServerAddr);
    }

    return Result;
}

// TODO(oleh): This is messy, refactor it.
static b32 HttpHeadersParse(web_arena *Arena,
                            web_string_view Buffer,
                            uz *Offset,
                            web_http_headers *Headers) {
    b32 Result = 1;
    uz I;

    for (I = *Offset; I < Buffer.Count; ++I) {
        if (Buffer.Items[I] == '\r' && Buffer.Count - I > 1 && Buffer.Items[I + 1] == '\n') goto Success;

        uz HeaderNameStart = I;

        for (; I < Buffer.Count; ++I) {
            if (Buffer.Items[I] == ':') break;
        }

        if (I >= Buffer.Count) return 0;

        web_string_view HeaderName = {.Items = Buffer.Items + HeaderNameStart, .Count = I - HeaderNameStart};

        uz HeaderValueStart = I + 1;

        for (I = HeaderValueStart; I < Buffer.Count; ++I) {
            if (Buffer.Items[I] == '\r') break;
        }

        if (Buffer.Count - I <= 1) return 0;
        if (Buffer.Items[I + 1] != '\n') return 0;

        web_string_view HeaderValue = {.Items = Buffer.Items + I + 2, .Count = I - HeaderValueStart - 2};

        web_http_header Header = {.Name = HeaderName, .Value = HeaderValue};

        WEB_ARRAY_PUSH(Arena, Headers, Header);
        ++I;
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

b32 WebHttpRequestParse(web_arena *Arena, web_string_view Buffer, web_http_request *OutRequest) {
    // 1. Request line. (https://datatracker.ietf.org/doc/html/rfc2616#section-5.1)
    // 1.1. Method. (https://datatracker.ietf.org/doc/html/rfc2616#section-5.1.1)

    uz I;
    for (I = 0; I < Buffer.Count; ++I) {
        if (Buffer.Items[I] == ' ') break;
    }

    if (I >= Buffer.Count) return 0;

    web_http_method RequestMethod;
    web_string_view RequestMethodSv = {.Items = Buffer.Items, .Count = I};
#define X(Method) if (WebStringViewEqualCStr(RequestMethodSv, #Method)) {  \
        RequestMethod = HTTP_##Method;                                  \
        goto RequestMethodSuccess;                                      \
    }

    WEB_ENUM_HTTP_METHODS

#undef X

    // Failed to parse the HTTP method.
    return 0;

 RequestMethodSuccess: ;
    // 1.2. Request URI. (https://datatracker.ietf.org/doc/html/rfc2616#section-5.1.2)
    // FIXME(oleh): Actually parse URI's.

    uz PathStart = I + 1;

    for (I = PathStart; I < Buffer.Count; ++I) {
        if (Buffer.Items[I] == ' ') break;
    }

    if (I >= Buffer.Count) return 0;

    web_string_view RequestPath = {.Items = Buffer.Items + PathStart, .Count = I - PathStart};

    // 1.3. HTTP version.

    uz VersionStart = I + 1;

    for (I = VersionStart; I < Buffer.Count; ++I) {
        if (Buffer.Items[I] == '\r') break;
    }

    if (I >= Buffer.Count) return 0;

    web_http_version RequestVersion;
    web_string_view VersionSv = {.Items = Buffer.Items + VersionStart, .Count = I - VersionStart};

#define X(Version, String) if (WebStringViewEqualCStr(VersionSv, String)) { \
        RequestVersion = HTTP_##Version;                                \
        goto RequestVersionSuccess;                                     \
    }

    WEB_ENUM_HTTP_VERSIONS
#undef X

    // NOTE(oleh): Failed to recognize the HTTP version.
    return 0;
 RequestVersionSuccess:
    // 1.4. CRLF.

    if (Buffer.Count - I <= 1) return 0;
    if (Buffer.Items[I + 1] != '\n') return 0;

    Buffer.Items += I + 2;
    Buffer.Count -= I + 2;

    // 2. Headers. (https://datatracker.ietf.org/doc/html/rfc2616#section-5.3)
    // FIXME(oleh): Actually parse the headers according to their specification.

    web_http_headers Headers;
    WEB_ARRAY_INIT(Arena, &Headers);

    I = 0;

    if (!HttpHeadersParse(Arena, Buffer, &I, &Headers)) return 0;

    if (I >= Buffer.Count) return 0;

    // 3. Message body. (https://datatracker.ietf.org/doc/html/rfc2616#section-4.3)
    web_string_view RequestBody = {.Items = Buffer.Items + I + 2, .Count = Buffer.Count - I - 2};

    OutRequest->Method = RequestMethod;
    OutRequest->Path = RequestPath;
    OutRequest->Version = RequestVersion;
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

#define DEFAULT_REQUEST_ARENA_CAPACITY (4ll * 1024ll * 1024ll * 1024ll)

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

    web_arena RequestArena;
    WebArenaInit(&RequestArena, DEFAULT_REQUEST_ARENA_CAPACITY);

    web_http_response_context ResponseContext = {0};
    ResponseContext.Arena = &RequestArena;

    while (1) {
    ServerLoopStart:
        WebArenaReset(&RequestArena);
        ResponseContext.ResponseHeaders.Count = 0;
        ResponseContext.Content = (web_string_view) {0};

        int ClientSock = accept(ServerSock, (struct sockaddr*)&ClientAddr, &ClientAddrSize);
        if (ClientSock == -1) {
            int AcceptError = errno;
            close(ServerSock);
            WEB_PANIC_FMT("Could not accept a new connection: %s", strerror(AcceptError));
        }

        ssize_t ReceivedBytesCount = recv(ClientSock, RequestArena.Items, RequestArena.Capacity, 0);
        if (ReceivedBytesCount == -1) {
            printf("Could not receive data from the socket\n");
            close(ClientSock);
            continue;
        }

        WEB_ASSERT(RequestArena.Offset == 0);

        web_string_view ParseBuffer = {.Items = RequestArena.Items, .Count = ReceivedBytesCount};

        RequestArena.Offset = WebAlignForward(ReceivedBytesCount, sizeof(uz));

        web_http_request HttpRequest;
        b32 Success = WebHttpRequestParse(&RequestArena, ParseBuffer, &HttpRequest);
        if (!Success) {
            printf("Could not parse the HTTP request\n");
            close(ClientSock);
            continue;
        }

        for (uz HandlerIndex = 0; HandlerIndex < Server->HandlersCount; ++HandlerIndex) {
            web_string_view HandlerPath = Server->HandlersPaths[HandlerIndex];
            if (!WebStringViewEqual(HandlerPath, HttpRequest.Path)) continue;

            web_http_request_handler Handler = Server->Handlers[HandlerIndex];

            ResponseContext.Request = HttpRequest;
            web_http_response_status ResponseStatus = Handler(&ResponseContext);

            // 1. Status line. (https://datatracker.ietf.org/doc/html/rfc2616#section-6.1)

            const char *ReasonPhrase = GetHttpResponseStatusReasonPhrase(ResponseStatus);
            const char *VersionString = HttpVersionStrings[HttpRequest.Version];

            web_dynamic_string ResponseHeadersString;
            WEB_ARRAY_INIT(ResponseContext.Arena, &ResponseHeadersString);

            HttpHeadersFormat(ResponseContext.Arena, &ResponseHeadersString, ResponseContext.ResponseHeaders);

            web_string_view ResponseString = WebArenaFormat(&RequestArena,
                                                            "%s %u %s\r\nAccess-Control-Allow-Origin: *\r\n" WEB_SV_FMT "\r\n" WEB_SV_FMT,
                                                            VersionString,
                                                            ResponseStatus,
                                                            ReasonPhrase,
                                                            WEB_SV_ARG(ResponseHeadersString),
                                                            WEB_SV_ARG(ResponseContext.Content));

            int SendStatus = send(ClientSock, ResponseString.Items, ResponseString.Count, 0);
            WEB_ASSERT(SendStatus != -1);

            close(ClientSock);

            goto ServerLoopStart;
        }

        web_http_response_status ResponseStatus = HTTP_STATUS_NOT_FOUND;
        const char *ReasonPhrase = GetHttpResponseStatusReasonPhrase(ResponseStatus);
        const char *VersionString = HttpVersionStrings[HttpRequest.Version];

        // TODO(oleh): No handler found, just give em 404!
        web_string_view ResponseString = WebArenaFormat(&RequestArena,
                                                 "%s %u %s\r\nAccess-Control-Allow-Origin: *\r\n\r\n",
                                                 VersionString,
                                                 ResponseStatus,
                                                 ReasonPhrase);

        int SendStatus = send(ClientSock, ResponseString.Items, ResponseString.Count, 0);
        WEB_ASSERT(SendStatus != -1);

        close(ClientSock);
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

void WebHttpServerInit(web_http_server *Server) {
    WebArenaInit(&Server->Arena, HTTP_SERVER_ARENA_CAPACITY);

    Server->Handlers = WebArenaPush(&Server->Arena, sizeof(*Server->Handlers) * HTTP_SERVER_MAX_HANDLERS);
    Server->HandlersPaths = WebArenaPush(&Server->Arena, sizeof(*Server->HandlersPaths) * HTTP_SERVER_MAX_HANDLERS);

    Server->HandlersCount = 0;
}

void WebHttpContextAddHeader(web_http_response_context *Ctx, web_string_view Name, web_string_view Value) {
    web_http_header Header = {
        .Name = Name,
        .Value = Value,
    };
    WEB_ARRAY_PUSH(Ctx->Arena, &Ctx->ResponseHeaders, Header);
}
