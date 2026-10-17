#include "http.h"
#include "https.h"
#include "test.h"
#include "threadpool.h"
#include "log.h"

#include <ctype.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>

#include <errno.h>

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
    if (WorkerData->Context->UseHttps) {
        return HttpsRead(&WorkerData->HttpsSession,
                         Buffer,
                         BufferCapacity);
    } else {
        return read(WorkerData->Sock, Buffer, BufferCapacity);
    }
}

static web_http_header HeaderAt(web_http_headers *Headers, sz Idx) {
    WEB_ASSERT(Idx < Headers->Capacity);
    return (web_http_header){.Name = Headers->Keys[Idx], .Value = Headers->Values[Idx]};
}

static void HttpHeadersFormat(web_arena *Arena, web_dynamic_string *String, web_http_headers Headers) {
    WEB_MAP_FOREACH(&Headers, HeaderIdx) {
        web_string_view HeaderName = Headers.Keys[HeaderIdx];
        web_http_header_value HeaderValue = Headers.Values[HeaderIdx];

        for (sz I = 0; I < HeaderName.Count; ++I) {
            WEB_ARRAY_PUSH(Arena, String, HeaderName.Items[I]);
        }

        WEB_ARRAY_PUSH(Arena, String, ':');
        WEB_ARRAY_PUSH(Arena, String, ' ');

        for (sz I = 0; I < HeaderValue.Count - 1; ++I) {
            WEB_ARRAY_EXTEND(Arena, String, &HeaderValue.Items[I]);
            WEB_ARRAY_PUSH(Arena, String, ',');
        }

        if (HeaderValue.Count > 0) {
            web_string_view LastValue = HeaderValue.Items[HeaderValue.Count - 1];
            WEB_ARRAY_EXTEND(Arena, String, &LastValue);
        }

        WEB_ARRAY_PUSH(Arena, String, '\r');
        WEB_ARRAY_PUSH(Arena, String, '\n');
    }
}

#define WEB_HTTP_RESPONSE_MAX_SIZE (128l * 1024l * 512l)

static web_string_view GetHTTPVersionString(web_arena *Arena, web_http_version Version) {
    return WebArenaFormat(Arena, "HTTP/%u.%u", Version.Major, Version.Minor);
}

static web_string_view HttpRequestToString(web_arena *Arena, web_http_request Request) {
    web_string_view VersionString = GetHTTPVersionString(Arena, Request.Version);

    const char *MethodString = HttpMethodNames[Request.Method];
    web_string_view MethodSv = WEB_SV_LIT(MethodString);

    web_dynamic_string RequestString;
    WEB_ARRAY_INIT(Arena, &RequestString);

    // Request line.
    WEB_ARRAY_EXTEND(Arena, &RequestString, &MethodSv);
    WEB_ARRAY_PUSH(Arena, &RequestString, ' ');
    WEB_ARRAY_EXTEND(Arena, &RequestString, &Request.Path);
    WEB_ARRAY_PUSH(Arena, &RequestString, ' ');
    WEB_ARRAY_EXTEND(Arena, &RequestString, &VersionString);
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
    })
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

static const char *ListValueHeaderNamesItems[] = {
    "Connection",
};

// @Sync(oleh): Need some sort of guard here when the tests are paralellized.
static WEB_ARRAY_TYPE(const char *) ListValueHeaderNames = {
    .Items = ListValueHeaderNamesItems,
    .Count = WEB_ARRAY_COUNT(ListValueHeaderNamesItems),
    .Capacity = WEB_ARRAY_COUNT(ListValueHeaderNamesItems),
};

// TODO(oleh): Get a list of all the headers with list values.
static b32 IsHeaderValueAList(web_string_view Name) {
    for (sz HeaderNameIdx = 0; HeaderNameIdx < ListValueHeaderNames.Count; ++HeaderNameIdx) {
        const char *ListValueHeaderName = ListValueHeaderNames.Items[HeaderNameIdx];
        if (WebStringViewEqualCStr(Name, ListValueHeaderName)) {
            return 1;
        }
    }

    return 0;
}

static void InsertPotentiallyCommaSeparatedHeaderValue(
        web_arena *Arena,
        web_http_headers *Headers,
        web_string_view Name,
        web_string_view Value
) {
    web_string_view Delim = WEB_SV_LIT(",");

    if (!IsHeaderValueAList(Name)) {
        WebHttpHeadersAdd(Arena, Headers, Name, Value);
        return;
    }

    while (Value.Count > 0) {
        web_string_view Part = WebStringViewChop(Value, Delim);
        WebHttpHeadersAdd(Arena, Headers, Name, Part);

        Value.Count -= Part.Count + Delim.Count;
        Value.Items += Part.Count + Delim.Count;
    }
}

static b32 IsLWS(u8 C) {
    return C == ' ' || C == '\t';
}

static sz ParseHeader(
        u8 *Buffer,
        sz BufferCount,
        web_string_view *HeaderName,
        web_string_view *HeaderValue
) {
    sz I = 0;

    for (; I < BufferCount; ++I) {
        if (Buffer[I] == ':') break;
    }

    if (I >= BufferCount) {
        return -1;
    }

    *HeaderName = (web_string_view){.Items = Buffer, .Count = I};

    I += 1;

    for (; I < BufferCount; ++I) {
        if (!IsLWS(Buffer[I])) break;
    }

    sz HeaderValueStart = I;

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

    *HeaderValue = (web_string_view){.Items = Buffer + HeaderValueStart, .Count = I - HeaderValueStart};

    sz TrailingSpace = 0;

    for (sz I = 0; I < HeaderValue->Count; ++I) {
        u8 Char = HeaderValue->Items[HeaderValue->Count - 1 - I];
        if (IsLWS(Char)) {
            TrailingSpace += 1;
        }
    }

    HeaderValue->Count -= TrailingSpace;

    return I + 2;
}

// TODO(oleh): This should be more strict, i.e. now it does not check that the ':' is before the CRLF.
static sz ParseHeaders(web_arena *Arena,
                       web_string_view Buffer,
                       web_http_headers *Headers) {
    WEB_ASSERT(Headers->Capacity != 0);

    for (sz CharIdx = 0; CharIdx < Buffer.Count; ) {
        if (Buffer.Items[CharIdx] == '\r' && Buffer.Count - CharIdx > 1 && Buffer.Items[CharIdx + 1] == '\n') {
            return CharIdx + 2;
        }

        web_string_view HeaderName = {0};
        web_string_view HeaderValue = {0};

        sz N = ParseHeader(Buffer.Items + CharIdx, Buffer.Count - CharIdx, &HeaderName, &HeaderValue);
        if (N < 0) {
            return -1;
        }

        InsertPotentiallyCommaSeparatedHeaderValue(Arena, Headers, HeaderName, HeaderValue);

        CharIdx += N;
    }

    return -1;
}

void WebHttpHeadersAdd(web_arena *Arena, web_http_headers *Headers, web_string_view Name, web_string_view Value) {
    web_http_header_value Values = {0};
    if (!WEB_MAP_GET(Headers, Name, &Values)) {
        WEB_ARRAY_INIT(Arena, &Values);
    }

    WEB_ARRAY_PUSH(Arena, &Values, Value);

    WEB_MAP_INSERT(Arena, Headers, Name, Values);
}

b32 WebHttpHeadersGet(const web_http_headers *Headers, web_string_view Name, web_http_header *Header) {
    if (WEB_MAP_GET(Headers, Name, &Header->Value)) {
        Header->Name = Name;
        return 1;
    }

    return 0;
}

static b32 ParseHTTPVersion_Fast(web_string_view Input, web_http_version *Version) {
#define X(Major, Minor, VersionString) if (WebStringViewEqualCStr(Input, VersionString)) { \
    Version->Number = HTTP_##Major##_##Minor; \
    return 1; \
    }
WEB_ENUM_HTTP_VERSIONS
#undef X

    return 0;
}

static b32 ParseHTTPVersion_Slow_Component(web_string_view *Input, u16 *OutComponent) {
    u32 Component = 0;

    sz CharIdx = 0;

    for (; CharIdx < Input->Count; ++CharIdx) {
        u8 Char = Input->Items[CharIdx];

        if (isdigit(Char)) {
            Component = Component * 10 + Char - '0';

            if (Component > UINT16_MAX) {
                WEB_LOG_FMT(DEBUG, HTTP, "Parsed HTTP version component '%u' is out of range", Component);
                return 0;
            }
        } else if (Char == '.') {
            break;
        } else {
            return 0;
        }
    }

    WEB_ASSERT(Component <= UINT16_MAX);
    *OutComponent = (u16) Component;

    Input->Count -= CharIdx;
    Input->Items += CharIdx;

    return 1;
}

static b32 ParseHTTPVersion_Slow(web_string_view Input, web_http_version *Version) {
    web_string_view Prefix = WEB_SV_LIT("HTTP/");
    if (!WebStringViewHasPrefix(Input, Prefix)) {
        return 0;
    }

    Input.Count -= Prefix.Count;
    Input.Items += Prefix.Count;

    if (!ParseHTTPVersion_Slow_Component(&Input, &Version->Major)) {
        return 0;
    }

    // NOTE(oleh): No dot found. String of form 'HTTP/<num>'
    if (Input.Count == 0) {
        return 0;
    }

    WEB_ASSERT(Input.Items[0] == '.');

    Input.Items += 1;
    Input.Count -= 1;

    if (!ParseHTTPVersion_Slow_Component(&Input, &Version->Minor)) {
        return 0;
    }

    // NOTE(oleh): Trailing garbage bytes.
    if (Input.Count != 0) {
        return 0;
    }

    return 1;
}

// @Spec(oleh): https://www.rfc-editor.org/info/rfc2616/#section-3.1.
// TODO(oleh): Check if the version is supported by us or no.
static b32 ParseHTTPVersion(web_string_view Input, web_http_version *Version) {
    return ParseHTTPVersion_Fast(Input, Version) || ParseHTTPVersion_Slow(Input, Version);
}

static b32 ParseStatusCode(web_string_view Buffer, web_http_response_status *StatusCode) {
    s64 StatusCodeNum;
    if (!WebParseS64(Buffer, &StatusCodeNum)) return 0;

#define X(Name, Code, Reason) if (StatusCodeNum == Code) { \
    *StatusCode = HTTP_STATUS_##Name; \
    return 1; \
    }
WEB_ENUM_HTTP_RESPONSE_STATUSES
#undef X

    WEB_LOG_FMT(
            DEBUG,
            HTTP,
            "Unsupported status code %ld",
            StatusCodeNum
    );

    return 0;
}

static sz ParseStatusLine(web_string_view Buffer,
                          web_http_version *Version,
                          web_http_response_status *StatusCode) {
    sz I;
    for (I = 0; I < Buffer.Count; ++I) {
        if (Buffer.Items[I] == ' ') break;
    }

    if (I >= Buffer.Count) return -1;

    web_string_view HttpVersionSv = {.Items = Buffer.Items, .Count = I};

    if (!ParseHTTPVersion(HttpVersionSv, Version)) return -1;

    uz StatusCodeStart = I + 1;

    for (I = StatusCodeStart; I < Buffer.Count; ++I) {
        if (Buffer.Items[I] == ' ') break;
    }

    if (I >= Buffer.Count) return -1;

    web_string_view StatusCodeSv = {.Items = Buffer.Items + StatusCodeStart, .Count = I - StatusCodeStart};

    if (!ParseStatusCode(StatusCodeSv, StatusCode)) return -1;

    sz ReasonStart = I + 1;
    for (I = ReasonStart; I < Buffer.Count; ++I) {
        if (Buffer.Items[I] == '\r') break;
    }

    if (I + 1 >= Buffer.Count || Buffer.Items[I + 1] != '\n') return -1;

    return I + 2;
}

b32 WebHttpResponseParse(web_arena *Arena, web_string_view Buffer, web_http_response *OutResponse) {
    // 1. Response line. (https://datatracker.ietf.org/doc/html/rfc2616#section-6.1)
    // 1.1. Version.

    sz I = ParseStatusLine(Buffer,
                           &OutResponse->Version,
                           &OutResponse->Status);
    if (I < 0) {
        return 0;
    }

    web_http_headers Headers;
    WEB_STRING_MAP_INIT(Arena, &Headers);

    Buffer.Items += I;
    Buffer.Count -= I;

    sz N = ParseHeaders(Arena, Buffer, &Headers);
    if (N < 0) {
        return 0;
    }

    I += N;

    web_string_view ResponseBody = {.Items = Buffer.Items + I, .Count = Buffer.Count - I};

    OutResponse->Body = ResponseBody;
    OutResponse->Headers = Headers;

    return 1;
}

// @Spec(oleh): https://www.rfc-editor.org/info/rfc2616/#section-5.1.1
static b32 ParseHTTPMethod(web_string_view Input, web_http_method *Out) {
#define X(Method) if (WebStringViewEqualCStr(Input, #Method)) {  \
    *Out = HTTP_##Method;                                  \
    return 1; \
    }

WEB_ENUM_HTTP_METHODS

#undef X

    return 0;
}

static b32 ReadRequestLineChunk(web_string_view Buffer, sz *Pointer, web_string_view *Chunk) {
    sz Start = *Pointer;

    for (; *Pointer < Buffer.Count; ++*Pointer) {
        u8 Char = Buffer.Items[*Pointer];
        if (Char == ' ') {
            break;
        }
        if (Char == '\r' || Char == '\n') {
            return 0;
        }
    }

    if (*Pointer >= Buffer.Count) {
        return 0;
    }

    Chunk->Items = Buffer.Items + Start;
    Chunk->Count = *Pointer - Start;

    *Pointer += 1;

    return 1;
}

// @Spec(oleh): https://www.rfc-editor.org/info/rfc2616/#section-5.1
static sz ParseRequestLine(web_string_view Buffer,
                           web_http_method *Method,
                           web_string_view *Path,
                           web_http_version *Version) {
    sz ParseOffset = 0;

    web_string_view RequestMethodSv = {0};
    if (!ReadRequestLineChunk(Buffer, &ParseOffset, &RequestMethodSv)) {
        return -1;
    }

    web_http_method RequestMethod;
    if (!ParseHTTPMethod(RequestMethodSv, &RequestMethod)) {
        return -1;
    }

    // @Spec(oleh): https://datatracker.ietf.org/doc/html/rfc2616#section-5.1.2
    // FIXME(oleh): Actually parse URI's.

    web_string_view RequestPath = {0};
    if (!ReadRequestLineChunk(Buffer, &ParseOffset, &RequestPath)) {
        return -1;
    }

    uz VersionStart = ParseOffset;

    for (ParseOffset = VersionStart; ParseOffset < Buffer.Count - 1; ++ParseOffset) {
        if (Buffer.Items[ParseOffset] == '\r' && Buffer.Items[ParseOffset + 1] == '\n') break;
    }

    if (ParseOffset >= Buffer.Count - 1) {
        return -1;
    }

    web_http_version RequestVersion;
    web_string_view RequestVersionSv = {.Items = Buffer.Items + VersionStart, .Count = ParseOffset - VersionStart};

    if (!ParseHTTPVersion(RequestVersionSv, &RequestVersion)) {
        return -1;
    }

    *Method = RequestMethod;
    *Path = RequestPath;
    *Version = RequestVersion;

    return ParseOffset + 2;
}

b32 WebHttpRequestParse(web_arena *Arena, web_string_view Buffer, web_http_request *OutRequest, web_string_view *Error) {
    // 1. Request line. (https://datatracker.ietf.org/doc/html/rfc2616#section-5.1)
    // 1.1. Method. (https://datatracker.ietf.org/doc/html/rfc2616#section-5.1.1)
    sz N = ParseRequestLine(Buffer,
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
    WEB_STRING_MAP_INIT(Arena, &Headers);

    sz I = 0;

    N = ParseHeaders(Arena, Buffer, &Headers);
    if (N < 0) {
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

web_http_request WebHttpRequestBuild(
        web_http_context *Context,
        web_string_view Path,
        web_http_method Method,
        web_string_view Body,
        ...
) {
    web_http_headers Headers = {0};
    WEB_STRING_MAP_INIT(&Context->Arena, &Headers);

    va_list HeaderArgs;
    va_start(HeaderArgs, Body);

    for (;;) {
        const char *HeaderName = va_arg(HeaderArgs, const char *);
        if (HeaderName == NULL) {
            break;
        }

        const char *HeaderValue = va_arg(HeaderArgs, const char *);
        if (HeaderValue == NULL) {
            WEB_PANIC_FMT("Odd amount of header values passed to %s", __FUNCTION__);
        }

        WebHttpHeadersAdd(&Context->Arena, &Headers, WEB_SV_LIT(HeaderName), WEB_SV_LIT(HeaderValue));
    }

    va_end(HeaderArgs);

    return (web_http_request){
        .Body = Body,
        .Headers = Headers,
        .Method = Method,
        .Path = Path,
        .Version = Context->HTTPVersion,
    };
}

typedef struct {
    web_string_view Data;
    web_string_view Extensions;
} response_chunk;

#define CONTENT_LENGTH_HEADER "Content-Length"
#define TRANSFER_ENCODING_HEADER "Transfer-Encoding"

static void ParseContentLength(web_http_header Header, s64 *ContentLength) {
    if (Header.Value.Count != 1) {
        WEB_LOG_FMT(
                WARN,
                HTTP,
                "Expected the '" CONTENT_LENGTH_HEADER "' value to have 1 component, got %ld instead",
                Header.Value.Count
        );
        goto Fail;
    }

    web_string_view ContentLengthString = Header.Value.Items[0];

    b32 Ok = WebParseS64(ContentLengthString, ContentLength);
    if (!Ok) {
        WEB_LOG_FMT(WARN,
                    HTTP,
                    "Failed to parse received " CONTENT_LENGTH_HEADER "header value as an integer; value=" WEB_SV_FMT,
                    WEB_SV_ARG(Header.Value));
        goto Fail;
    }

Fail:
        *ContentLength = -1;
}

static b32 HeaderValueContains(web_http_header_value Value, const char *Needle) {
    for (sz TokenIdx = 0; TokenIdx < Value.Count; ++TokenIdx) {
        web_string_view ConnectionToken = Value.Items[TokenIdx];
        if (WebStringViewEqualCStr(ConnectionToken, Needle)) {
            return 1;
        }
    }

    return 0;
}

typedef enum {
    PARSE_CHUNK_OK,
    PARSE_CHUNK_ERROR,
    PARSE_CHUNK_MORE,
} parse_chunk_result;

// TODO(oleh): Handle extensions.
static parse_chunk_result ParseChunk(web_string_view Buffer, response_chunk *Chunk, sz *ChunkSize) {
    WEB_STRUCT_ZERO(Chunk);

    if (Buffer.Count == 0) {
        WEB_LOG(DEBUG, HTTP, "Received an empty buffer");
        return PARSE_CHUNK_ERROR;
    }

    u32 ChunkLength = 0;
    s32 NumMatches = sscanf((const char *)Buffer.Items, "%x", &ChunkLength);
    if (NumMatches != 1) {
        web_string_view BufferStart = {.Items = Buffer.Items, .Count = WEB_MIN(Buffer.Count, 4)};
        WEB_LOG_FMT(
                DEBUG,
                HTTP,
                "Failed to read the chunk's length (" WEB_SV_FMT ") as a valid hex integer",
                WEB_SV_ARG(BufferStart)
        );
        return PARSE_CHUNK_ERROR;
    }

    sz Offset = 0;

    for (; Offset < Buffer.Count; ++Offset) {
        if (Buffer.Items[Offset] == '\r') break;
    }

    if (Offset + 1 >= Buffer.Count) {
        WEB_LOG(DEBUG, HTTP, "EOF while looking for CRLF");
        return PARSE_CHUNK_ERROR;
    }

    if (Buffer.Items[Offset + 1] != '\n') {
        WEB_LOG(DEBUG, HTTP, "Malformed CRLF");
        return PARSE_CHUNK_ERROR;
    }

    Offset += 2;

    sz Avail = Buffer.Count - Offset;
    if (Avail < ChunkLength) {
        *ChunkSize = ChunkLength;
        return PARSE_CHUNK_MORE;
    }

    Chunk->Data.Items = Buffer.Items + Offset;
    Chunk->Data.Count = ChunkLength;

    if (ChunkLength == 0) {
        *ChunkSize = Offset;
        return PARSE_CHUNK_OK;
    }

    Offset += ChunkLength;

    if (Offset + 1 >= Buffer.Count) {
        WEB_LOG(DEBUG, HTTP, "EOF while looking for CRLF");
        return PARSE_CHUNK_ERROR;
    }

    if (Buffer.Items[Offset] != '\r' || Buffer.Items[Offset + 1] != '\n') {
        WEB_LOG(DEBUG, HTTP, "Malformed CRLF");
        return PARSE_CHUNK_ERROR;
    }

    Offset += 2;

    *ChunkSize = Offset;

    return PARSE_CHUNK_OK;
}

static web_string_view MergeChunks(u8 *Buffer, sz BufferCount, response_chunk *Chunks, sz ChunksCount) {
    sz Offset = 0;

    for (sz ChunkIdx = 0; ChunkIdx < ChunksCount; ++ChunkIdx) {
        response_chunk Chunk = Chunks[ChunkIdx];

        if (Chunk.Data.Count > BufferCount - Offset) {
            return (web_string_view){0};
        }

        memcpy(Buffer + Offset, Chunk.Data.Items, Chunk.Data.Count);
        Offset += Chunk.Data.Count;
    }

    return (web_string_view){.Items = Buffer, .Count = Offset};
}

#define MAX_RESPONSE_LENGTH 4 * 1024 * 1024

static b32 HttpResponseParseStreaming(worker_data *WorkerData, web_http_response *Resp) {
    b32 ParseResult = 0;

#define OK() do { ParseResult = 1; goto ParseSuccess; } while (0)
#define ERR() do { ParseResult = 0; goto ParseFail; } while (0)

    web_temp Temp = WebGetTempArena();

    web_arena *Arena = &WorkerData->Context->Arena;

    web_http_version Version = {0};
    web_http_response_status Code = 0;

    web_http_headers Headers = {0};
    WEB_STRING_MAP_INIT(Arena, &Headers);

    WEB_ARRAY_TYPE(response_chunk) ResponseChunks = {0};
    WEB_ARRAY_INIT(&Temp.Arena, &ResponseChunks);

    uz BufferCount = 0;
    uz BufferCapacity = MAX_RESPONSE_LENGTH;
    u8 *Buffer = WebArenaPush(Arena, BufferCapacity, 1);

    sz ParseOffset = 0;

    s64 ContentLength = -1;

    b32 ChunkedTransfer = 0;
    b32 ParsedHeaders = 0;

ReceiveLoop: ;
    if (BufferCount >= BufferCapacity) {
        WEB_LOG(WARN, HTTP, "Response size exceeds maximum configured length");
        ERR();
    }

    sz Received = HttpReceive(WorkerData, Buffer + BufferCount, BufferCapacity - BufferCount);
    if (Received < 0) {
        ERR();
    }

    BufferCount += Received;

    if (ParsedHeaders) {
        goto ParseBody;
    }

    web_string_view StatusLineSv = {.Items = Buffer + ParseOffset, .Count = BufferCount - ParseOffset};

    sz StatusLineLength = ParseStatusLine(
            StatusLineSv,
            &Version,
            &Code
    );
    if (StatusLineLength < 0) {
        ERR();
    }

    ParseOffset += StatusLineLength;

    web_string_view HeadersSv = {.Items = Buffer + ParseOffset, .Count = BufferCount - ParseOffset};
    sz HeadersLength = ParseHeaders(Arena, HeadersSv, &Headers);
    if (StatusLineLength < 0) {
        ERR();
    }

    ParsedHeaders = 1;

    ParseOffset += HeadersLength;

    // TODO(oleh): Probably replace this with lookups.
    WEB_MAP_FOREACH(&Headers, HeaderIdx) {
        web_http_header Header = HeaderAt(&Headers, HeaderIdx);
        if (WebStringViewEqualCStr(Header.Name, CONTENT_LENGTH_HEADER)) {
            ParseContentLength(Header, &ContentLength);
        } else if (WebStringViewEqualCStr(Header.Name, TRANSFER_ENCODING_HEADER)) {
            ChunkedTransfer = HeaderValueContains(Header.Value, "chunked");
        }
    }

ParseBody:
    if (ChunkedTransfer) {
        web_string_view RawChunk = {.Items = Buffer + ParseOffset, .Count = BufferCount - ParseOffset};
        response_chunk Chunk = {0};
        sz ChunkBytesRead = 0;
        parse_chunk_result Result = ParseChunk(RawChunk, &Chunk, &ChunkBytesRead);

        switch (Result) {
            case PARSE_CHUNK_OK: {
                ParseOffset += ChunkBytesRead;

                if (Chunk.Data.Count == 0) {
                    Resp->Body = MergeChunks(
                            Buffer + ParseOffset,
                            BufferCapacity - ParseOffset,
                            ResponseChunks.Items,
                            ResponseChunks.Count
                    );

                    if (Resp->Body.Items == NULL) {
                        WEB_LOG(ERROR, HTTP, "Not enough memory to merge response chunks");
                        ERR();
                    }

                    OK();
                }

                WEB_ARRAY_PUSH(&Temp.Arena, &ResponseChunks, Chunk);
                goto ReceiveLoop;
            }
            case PARSE_CHUNK_ERROR: {
                WEB_LOG_FMT(DEBUG, HTTP, "Failed to parse response chunk at offset %ld", ParseOffset);
                ERR();
            }
            case PARSE_CHUNK_MORE: {
                goto ReceiveLoop;
            }
        }
    }

    if (ContentLength < 0) {
        Resp->Body = (web_string_view) {.Items = Buffer + ParseOffset, .Count = BufferCount - ParseOffset};
        OK();
    }

    sz Have = BufferCount - ParseOffset;
    if (Have >= ContentLength) {
        Resp->Body = (web_string_view) {.Items = Buffer + ParseOffset, .Count = ContentLength};
        OK();
    }

    if (Received <= 0) {
        WEB_LOG(DEBUG, HTTP, "Body EOF");
        ERR();
    }

    goto ReceiveLoop;

ParseSuccess:
    Resp->Headers = Headers;
    Resp->Version = Version;
    Resp->Status = Code;

ParseFail:
    if (ParseResult == 0) {
        web_string_view BufferSv = {.Items = Buffer, .Count = BufferCount};
        WEB_LOG_FMT(DEBUG, HTTP, "Whole parse buffer: " WEB_SV_FMT, WEB_SV_ARG(BufferSv));
    }

    WebReturnTempArena(Temp);

    return ParseResult;

#undef OK
#undef ERR
}

// NOTE(oleh): Not sure if it's fine to use temp here for most of the stuff as the temp arena is kinda
// expected to be small.
static void HttpRequestSendProc(void *DataPtr) {
    worker_data *WorkerData = (worker_data *) DataPtr;

    b32 Result = 1;

    web_temp Temp = WebGetTempArena();

    web_string_view RequestString = HttpRequestToString(&Temp.Arena, WorkerData->Request);

    // FIXME(oleh): Refer to the spec to see if this is actually a valid thing to do or it should be
    // headers + streamed body.
    while (RequestString.Count > 0) {
        sz N = HttpSend(WorkerData, RequestString);
        if (N < 0) {
            WEB_LOG(DEBUG, HTTP, "Failed to send data over the connection");
            Result = 0;
            goto End;
        }

        RequestString.Items += N;
        RequestString.Count -= N;
    }

    if (!HttpResponseParseStreaming(WorkerData, WorkerData->Response)) {
        WEB_LOG(DEBUG, HTTP, "Failed to parse server's response");
        Result = 0;
        goto End;
    }

End:
    WorkerData->StatusBit = Result;
    WebReturnTempArena(Temp);
}

static sz HttpsCloseConnection(web_https_session *Sess) {
    return Sess->VTable.Close(Sess->Data);
}

static b32 HeadersContainConnectionClose(const web_http_headers *Headers) {
    web_http_header ConnectionHeader = {0};
    if (!WebHttpHeadersGet(Headers, WEB_SV_LIT("Connection"), &ConnectionHeader)) {
        return 0;
    }

    return HeaderValueContains(ConnectionHeader.Value, "close");
}

b32 WebHttpRequestSend(web_http_context *Context,
                       web_string_view Hostname,
                       web_http_request Request,
                       web_http_response *Response) {
    web_temp Temp = WebGetTempArena();
    b32 Result = 1;

    struct addrinfo Hints = {0};
    struct addrinfo* ServerAddr = NULL;

    Hints.ai_family = AF_INET;
    Hints.ai_socktype = SOCK_STREAM;
    Hints.ai_flags = AI_PASSIVE;

    const char *HostnameCStr = WebStringViewCloneCStr(&Temp.Arena, Hostname);

    int Sock = -1;

    sz Status = getaddrinfo(HostnameCStr, "80", &Hints, &ServerAddr);
    if (Status != 0) {
        WEB_LOG_FMT(DEBUG, HTTP, "Call to 'getaddrinfo' failed with error code %ld", Status);
        Result = 0;
        goto End;
    }

    Sock = socket(ServerAddr->ai_family, ServerAddr->ai_socktype, 0);
    if (Sock == -1) {
        WEB_LOG_FMT(DEBUG, HTTP, "Call to 'socket' failed: %s", strerror(errno));
        Result = 0;
        goto End;
    }

    Status = connect(Sock, ServerAddr->ai_addr, ServerAddr->ai_addrlen);
    if (Status != 0) {
        WEB_LOG_FMT(DEBUG, HTTP, "Call to 'connect' failed: %s", strerror(errno));
        Result = 0;
        goto End;
    }

    web_https_session HttpsSession = {0};

    if (Context->UseHttps) {
        Status = HttpsConnect(Context->HttpsProvider, Sock, &HttpsSession);

        if (Status <= 0) WEB_TODO();
    }

    // TODO(oleh): Use a functional map impl here to avoid copies.
    if (!HeadersContainConnectionClose(&Request.Headers)) {
        web_http_headers NewHeaders = {0};
        WEB_MAP_COPY(&Context->Arena, &NewHeaders, &Request.Headers);
        WebHttpHeadersAdd(&Context->Arena, &NewHeaders, WEB_SV_LIT("Connection"), WEB_SV_LIT("close"));
        Request.Headers = NewHeaders;
    }

    worker_data *WorkerData = WebSyncPoolAlloc(&Context->WorkerPool);

    WorkerData->Context = Context;
    WorkerData->Sock = Sock;
    WorkerData->HttpsSession = HttpsSession;
    WorkerData->Request = Request;
    WorkerData->Response = Response;

    web_thread_pool_task *Task = WEB_ARENA_NEW(&Temp.Arena, typeof(*Task));
    WebThreadPoolTaskInit(Task, HttpRequestSendProc, WorkerData);

    WebThreadPoolScheduleTask(&Context->ThreadPool, Task);

    WebThreadPoolTaskWaitUntilCompletion(Task);

    Result = WorkerData->StatusBit;

End:
    WebReturnTempArena(Temp);

    if (ServerAddr != NULL) {
        freeaddrinfo(ServerAddr);
    }

    if (Context->UseHttps) {
        HttpsCloseConnection(&HttpsSession);
    }

    if (Sock != -1) {
        close(Sock);
    }

    return Result;
}

static const char *GetHTTPResponseStatusReasonPhrase(web_http_response_status Status) {
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

    sz BufferCount = 0;
    sz BufferCapacity = INITIAL_PARSE_BUFFER_CAPACITY;
    u8 *Buffer = WebArenaPush(Arena, BufferCapacity, 1);

    request_parse_state ParseState = PARSE_STATE_REQUEST_LINE;

    s64 ContentLength = -1;
    web_string_view HeaderName = {0};
    web_string_view HeaderValue = {0};
    WEB_STRING_MAP_INIT(Arena, &Request->Headers);

ReceiveLoop:
    if (BufferCount >= BufferCapacity) {
        BufferCapacity <<= 1;
        Buffer = WebArenaRealloc(Arena, Buffer, BufferCount, BufferCapacity, 1);
    }

    N = HttpReceive(WorkerData, Buffer + WriteOffset, BufferCapacity - WriteOffset);
    if (N == -1) {
        WEB_LOG(ERROR, HTTP, "Failed to receive from a client socket");
        return 0;
    }

    WriteOffset += N;
    BufferCount += N;

    switch (ParseState) {
    case PARSE_STATE_REQUEST_LINE: goto ParseRequestLine;
    case PARSE_STATE_HEADERS:      goto ParseHeaders;
    case PARSE_STATE_BODY:         goto ParseBody;
    }

ParseRequestLine: ;
    web_string_view RequestLineSv = {.Items = Buffer + ParseOffset, .Count = BufferCount - ParseOffset};

    N = ParseRequestLine(RequestLineSv,
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
    while (ParseOffset < BufferCount) {
        if (ParseOffset < BufferCount - 1 &&
            Buffer[ParseOffset] == '\r' && Buffer[ParseOffset + 1] == '\n') {
            ParseOffset += 2;
            ParseState = PARSE_STATE_BODY;
            break;
        }

        N = ParseHeader(Buffer + ParseOffset, BufferCount - ParseOffset, &HeaderName, &HeaderValue);
        if (N == -1) {
            WEB_LOG(ERROR, HTTP, "Failed to parse a request's header");
            return 0;
        }

        InsertPotentiallyCommaSeparatedHeaderValue(Arena, &Request->Headers, HeaderName, HeaderValue);

        ParseOffset += N;

        if (WebStringViewEqualCStr(HeaderName, CONTENT_LENGTH_HEADER)) {
            web_http_header Header = {0};
            WEB_ASSERT(WebHttpHeadersGet(&Request->Headers, HeaderName, &Header));
            ParseContentLength(Header, &ContentLength);
        }
    }

    if (ParseState == PARSE_STATE_HEADERS && ParseOffset >= BufferCount) goto ReceiveLoop;

ParseBody:
    if (ContentLength >= 0) {
        if (BufferCount - ParseOffset >= ContentLength) {
            Request->Body.Items = Buffer + ParseOffset;
            Request->Body.Count = ContentLength;
            return 1;
        }

        goto ReceiveLoop;
    } else {
        // TODO(oleh): Maybe this should go back to the receive loop with some timeout checks.
        Request->Body.Items = Buffer + ParseOffset;
        Request->Body.Count = BufferCount - ParseOffset;
        return 1;
    }
}

static void AddDefaultHeaders(web_arena *Arena, web_http_headers *Headers) {
    WebHttpHeadersAdd(Arena, Headers, WEB_SV_LIT("Access-Control-Allow-Origin"), WEB_SV_LIT("*"));
    WebHttpHeadersAdd(Arena, Headers, WEB_SV_LIT("Connection"), WEB_SV_LIT("close"));
}

static void ServerWorker(void *Arg) {
    worker_data *Data = (worker_data *)Arg;

HandleRequest: ;
    web_http_response_context *Ctx = WebSyncPoolAlloc(&Data->Context->ResponseContextPool);

    WebArenaReset(&Ctx->Arena);
    WEB_STRING_MAP_INIT(&Ctx->Arena, &Ctx->ResponseHeaders);
    WEB_STRUCT_ZERO(&Ctx->Content);

    web_http_request HttpRequest = {0};
    b32 RequestParsed = HttpRequestParseStreaming(Data, &Ctx->Arena, &HttpRequest);
    if (!RequestParsed) {
        WEB_LOG(INFO, HTTP, "Could not streaming parse HTTP request");
        goto Cleanup;
    }

    Ctx->Request = HttpRequest;

    AddDefaultHeaders(&Ctx->Arena, &Ctx->ResponseHeaders);

    for (uz HandlerIndex = 0; HandlerIndex < Data->Server->HandlersCount; ++HandlerIndex) {
        web_string_view HandlerPath = Data->Server->HandlersPaths[HandlerIndex];
        if (!WebStringViewEqual(HandlerPath, HttpRequest.Path)) continue;

        web_http_request_handler Handler = Data->Server->Handlers[HandlerIndex];

        web_http_response_status ResponseStatus = Handler(Ctx);

        // 1. Status line. (https://datatracker.ietf.org/doc/html/rfc2616#section-6.1)
        const char *ReasonPhrase = GetHTTPResponseStatusReasonPhrase(ResponseStatus);
        web_string_view VersionString = GetHTTPVersionString(&Ctx->Arena, HttpRequest.Version);

        web_dynamic_string ResponseHeadersString;
        WEB_ARRAY_INIT(&Ctx->Arena, &ResponseHeadersString);

        HttpHeadersFormat(&Ctx->Arena, &ResponseHeadersString, Ctx->ResponseHeaders);

        web_string_view ResponseString = WebArenaFormat(&Ctx->Arena,
                                                        WEB_SV_FMT " %u %s\r\n" WEB_SV_FMT "\r\n" WEB_SV_FMT,
                                                        WEB_SV_ARG(VersionString),
                                                        ResponseStatus,
                                                        ReasonPhrase,
                                                        WEB_SV_ARG(ResponseHeadersString),
                                                        WEB_SV_ARG(Ctx->Content));

        WEB_LOG_FMT(DEBUG, HTTP, "Sending the response string to the client:\r\n" WEB_SV_FMT, WEB_SV_ARG(ResponseString));

        sz NumSent = HttpSend(Data, ResponseString);
        WEB_VERIFY(NumSent > 0);

        goto Cleanup;
    }

    web_http_response_status ResponseStatus = HTTP_STATUS_NOT_FOUND;
    const char *ReasonPhrase = GetHTTPResponseStatusReasonPhrase(ResponseStatus);
    web_string_view VersionString = GetHTTPVersionString(&Ctx->Arena, HttpRequest.Version);

    web_dynamic_string HeadersString = {0};
    HttpHeadersFormat(&Ctx->Arena, &HeadersString, Ctx->ResponseHeaders);

    // TODO(oleh): No handler found, just give em 404!
    web_string_view ResponseString = WebArenaFormat(&Ctx->Arena,
                                                    WEB_SV_FMT " %u %s\r\n" WEB_SV_FMT "\r\n" WEB_SV_FMT,
                                                    WEB_SV_ARG(VersionString),
                                                    ResponseStatus,
                                                    ReasonPhrase,
                                                    WEB_SV_ARG(HeadersString),
                                                    WEB_SV_ARG(Ctx->Content));

    int SendStatus = HttpSend(Data, ResponseString);
    WEB_ASSERT(SendStatus != -1);

Cleanup:
    WebSyncPoolFree(&Data->Context->ResponseContextPool, Ctx);

    web_http_header ConnectionHeader = {0};

    if (!RequestParsed || Ctx->Request.Version.Number == HTTP_1_0) {
        goto CloseConnection;
    }

    if (HeadersContainConnectionClose(&Ctx->Request.Headers) || HeadersContainConnectionClose(&Ctx->ResponseHeaders)) {
        goto CloseConnection;
    }

    if (WebHttpHeadersGet(&Ctx->Request.Headers, WEB_SV_LIT("Connection"), &ConnectionHeader)) {
        for (sz TokenIdx = 0; TokenIdx < ConnectionHeader.Value.Count; ++TokenIdx) {
            web_string_view ConnectionToken = ConnectionHeader.Value.Items[TokenIdx];
            if (WebStringViewEqualCStr(ConnectionToken, "close")) {
                goto CloseConnection;
            }
        }
    }

    goto HandleRequest;

CloseConnection:
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
    })
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
#define HTTP_CONTEXT_ARENA_CAPACITY (4ll * 1024ll * 1024ll * 1024ll)

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
    WEB_CASE_PROVIDER_OPENSSL({
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
    })
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
    WebArenaInitFixed(&ResponseContext->Arena, DEFAULT_REQUEST_ARENA_CAPACITY);
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

b32 WebHttpContextInit(web_http_context_config Config, web_http_context *Context) {
    if (Config.GlobalPoolCapacity <= 0) {
        Config.GlobalPoolCapacity = HTTP_CONTEXT_ARENA_CAPACITY;
    }

    if (Config.HTTPVersion.Number <= 0) {
        Config.HTTPVersion.Number = HTTP_1_1;
    } else if (Config.HTTPVersion.Number > HTTP_1_1) {
        web_temp Temp = WebGetTempArena();
        web_string_view VersionString = GetHTTPVersionString(&Temp.Arena, Config.HTTPVersion);

        WEB_LOG_FMT(ERROR, HTTP, "Unsupported HTTP version " WEB_SV_FMT, WEB_SV_ARG(VersionString));
        return 0;
    }

    Context->HTTPVersion = Config.HTTPVersion;

    WebArenaInitChained(&Context->Arena, Config.GlobalPoolCapacity);

    WebSyncPoolInit(&Context->WorkerPool, NewWorkerDataPoolProc);
    WebSyncPoolInit(&Context->TaskPool, NewTaskProc);
    WebSyncPoolInit(&Context->ResponseContextPool, NewContextPoolProc);

    uz NumThreads = Config.NumThreads || 1;
    web_thread_pool_config ThreadPoolConfig = {.NumThreads = NumThreads};
    return WebThreadPoolInit(&Context->ThreadPool, &Context->Arena, &ThreadPoolConfig);
}

b32 WebHttpServerInit(web_http_context *Context, web_http_server *Server) {
    Server->Context = Context;

    if (Context->UseHttps) {
        WEB_VERIFY(Context->HttpsProvider != NULL);

        HttpsInit(Context->HttpsProvider);
    }

    Server->Handlers = WEB_ARENA_NEW_MANY(
            &Context->Arena,
            typeof(*Server->Handlers),
            HTTP_SERVER_MAX_HANDLERS
    );
    Server->HandlersPaths = WEB_ARENA_NEW_MANY(
            &Context->Arena,
            typeof(*Server->HandlersPaths),
            HTTP_SERVER_MAX_HANDLERS
    );

    Server->HandlersCount = 0;

    return 1;
}

void WebHttpContextAddHeader(web_http_response_context *Ctx, web_string_view Name, web_string_view Value) {
    WebHttpHeadersAdd(&Ctx->Arena, &Ctx->ResponseHeaders, Name, Value);
}

void WebHttpResponseWrite(web_http_response_context *Ctx, web_string_view Response) {
    Ctx->Content = Response;
}

// Tests
WEB_DEFINE_TEST(ParseHTTPVersionKnown) {
    web_string_view Input = WEB_SV_LIT("HTTP/1.1");

    web_http_version Version = {0};
    WEB_T_TRUE(ParseHTTPVersion(Input, &Version));

    WEB_T_EQUAL(Version.Major, 1);
    WEB_T_EQUAL(Version.Minor, 1);
    WEB_T_EQUAL(Version.Number, WEB_MAKE_HTTP_NUMBER(1, 1));
}

WEB_DEFINE_TEST(ParseHTTPVersionBig) {
    web_string_view Input = WEB_SV_LIT("HTTP/20.26");

    web_http_version Version = {0};
    WEB_T_TRUE(ParseHTTPVersion(Input, &Version));

    WEB_T_EQUAL(Version.Major, 20);
    WEB_T_EQUAL(Version.Minor, 26);
}

WEB_DEFINE_TEST(ParseRequestLineOK) {
    web_string_view RequestLine = WEB_SV_LIT("GET /index.html HTTP/1.1\r\n");

    web_http_method Method;
    web_string_view Path;
    web_http_version Version;

    WEB_T_NEQUAL(ParseRequestLine(RequestLine, &Method, &Path, &Version), -1);

    WEB_T_EQUAL(Method, HTTP_GET);
    WEB_T_EQUAL(Path, WEB_SV_LIT("/index.html"));
    WEB_T_EQUAL(Version.Number, HTTP_1_1);
}

WEB_DEFINE_TEST(ParseHeaderListValue) {
    const char *ListHeaderNames[] = {"H1", "H2", "H3"};

    for (sz HeaderIdx = 0; HeaderIdx < WEB_ARRAY_COUNT(ListHeaderNames); ++HeaderIdx) {
        const char *H = ListHeaderNames[HeaderIdx];
        WEB_ARRAY_PUSH(&Runner->Arena, &ListValueHeaderNames, H);
    }

    web_string_view Request = WEB_SV_LIT("H1: a,b,c\r\nH2: a\r\nH2: b\r\nH2: c\r\nH3: a\r\n\r\n");

    web_http_headers Headers = {0};
    WEB_STRING_MAP_INIT(&Runner->Arena, &Headers);

    sz HeadersLength = ParseHeaders(&Runner->Arena, Request, &Headers);

    for (sz HeaderIdx = 0; HeaderIdx < WEB_ARRAY_COUNT(ListHeaderNames); ++HeaderIdx) {
        WEB_ARRAY_POP(&ListValueHeaderNames);
    }

    WEB_T_NEQUAL(HeadersLength, -1);

    web_http_header Header = {0};
    WEB_T_TRUE(WebHttpHeadersGet(&Headers, WEB_SV_LIT("H1"), &Header));

    WEB_T_EQUAL(Header.Name, WEB_SV_LIT("H1"));
    WEB_T_EQUAL(Header.Value.Count, 3);

    WEB_T_EQUAL(Header.Value.Items[0], WEB_SV_LIT("a"));
    WEB_T_EQUAL(Header.Value.Items[1], WEB_SV_LIT("b"));
    WEB_T_EQUAL(Header.Value.Items[2], WEB_SV_LIT("c"));

    WEB_T_TRUE(WebHttpHeadersGet(&Headers, WEB_SV_LIT("H2"), &Header));

    WEB_T_EQUAL(Header.Value.Count, 3);

    WEB_T_EQUAL(Header.Value.Items[0], WEB_SV_LIT("a"));
    WEB_T_EQUAL(Header.Value.Items[1], WEB_SV_LIT("b"));
    WEB_T_EQUAL(Header.Value.Items[2], WEB_SV_LIT("c"));

    WEB_T_TRUE(WebHttpHeadersGet(&Headers, WEB_SV_LIT("H3"), &Header));

    WEB_T_EQUAL(Header.Value.Count, 1);

    WEB_T_EQUAL(Header.Value.Items[0], WEB_SV_LIT("a"));
}

WEB_DEFINE_TEST(ShouldCloseConnection) {
    const char *Inputs[] = {
        "Connection: close\r\n\r\n",
        "Connection: keep,close\r\n\r\n",
        "A: b\r\nConnection: close\r\n\r\n",
    };

    web_http_headers Headers = {0};
    WEB_STRING_MAP_INIT(&Runner->Arena, &Headers);

    for (sz I = 0; I < WEB_ARRAY_COUNT(Inputs); ++I) {
        web_string_view Input = WEB_SV_LIT(Inputs[I]);

        WEB_MAP_RESET(&Headers);
        WEB_T_NEQUAL(ParseHeaders(&Runner->Arena, Input, &Headers), -1);
        WEB_T_TRUE(HeadersContainConnectionClose(&Headers));
    }

    WEB_MAP_RESET(&Headers);
    WEB_T_NEQUAL(ParseHeaders(&Runner->Arena, WEB_SV_LIT("A: b\r\n\r\n"), &Headers), -1);
    WEB_T_FALSE(HeadersContainConnectionClose(&Headers));
}
