#include "../src/base64.h"
#include "../src/json.h"
#include "../src/utf.h"

#define SV_EQUAL(Lhs, Rhs) do { \
if (!WebStringViewEqual((Lhs), (Rhs))) WEB_PANIC_FMT("Assertion failed: '" WEB_SV_FMT "' != '" WEB_SV_FMT "'", WEB_SV_ARG((Lhs)), WEB_SV_ARG((Rhs))); \
    } while (0)

#define INT_EQUAL(Lhs, Rhs) do { \
    if ((Lhs) != (Rhs)) WEB_PANIC_FMT("Assertion failed: 0x%04x != 0x%04x", (Lhs), (Rhs)); \
} while (0)

void TestBase64(void) {
    web_string_view Input = WEB_SV_LIT("Many hands make light work.");

    uz BufferCount = 100;
    u8 Buffer[BufferCount];
    WebBase64Encode(Input, Buffer, &BufferCount);

    web_string_view Encoded;
    Encoded.Items = Buffer;
    Encoded.Count = BufferCount;

    SV_EQUAL(Encoded, WEB_SV_LIT("TWFueSBoYW5kcyBtYWtlIGxpZ2h0IHdvcmsu"));

    uz DBufferCount = Encoded.Count;
    u8 DBuffer[DBufferCount];
    WEB_ASSERT(WebBase64Decode(Encoded, DBuffer, &DBufferCount));

    web_string_view Decoded;
    Decoded.Items = DBuffer;
    Decoded.Count = DBufferCount;

    SV_EQUAL(Decoded, Input);
}

#define CODE_POINT_A 0x0430
#define CODE_POINT_B 0x0431

void TestUTF8Decoding(void) {
    web_utf8_stream Stream = {
        .View = WEB_SV_LIT("аб"),
    };

    web_code_point PointA = 0;
    WEB_VERIFY(WebUTF8StreamNext(&Stream, &PointA));
    INT_EQUAL(PointA, CODE_POINT_A);

    web_code_point PointB = 0;
    WEB_VERIFY(WebUTF8StreamNext(&Stream, &PointB));
    INT_EQUAL(PointB, CODE_POINT_B);

    WEB_VERIFY(!WebUTF8StreamNext(&Stream, NULL));
}

void TestUTF8Encoding(void) {
    web_code_point CodePoints[] = {
        CODE_POINT_A,
        CODE_POINT_B,
    };
    sz CodePointsCount = sizeof(CodePoints)/sizeof(*CodePoints);

    u8 Output[4] = {0};
    sz OutputCount = sizeof(Output)/sizeof(*Output);

    sz NumWritten = 0;

    WEB_VERIFY(WebUTF8Encode(CodePoints, CodePointsCount, Output, OutputCount, &NumWritten));

    INT_EQUAL((s32)NumWritten, 4);
    INT_EQUAL(Output[0], 0xC0 | (CODE_POINT_A >> 6));
    INT_EQUAL(Output[1], 0x80 | (CODE_POINT_A & 0x3F));
    INT_EQUAL(Output[2], 0xC0 | (CODE_POINT_B >> 6));
    INT_EQUAL(Output[3], 0x80 | (CODE_POINT_B & 0x3F));

    web_string_view Input = {.Items = Output, .Count = OutputCount};

    web_utf8_stream Stream = {.View = Input};

    web_code_point PointA = 0;
    WEB_VERIFY(WebUTF8StreamNext(&Stream, &PointA));
    INT_EQUAL(PointA, CODE_POINT_A);

    web_code_point PointB = 0;
    WEB_VERIFY(WebUTF8StreamNext(&Stream, &PointB));
    INT_EQUAL(PointB, CODE_POINT_B);

    WEB_VERIFY(!WebUTF8StreamNext(&Stream, NULL));
}

void TestJsonEncoding_StringEscaping(web_arena *Arena) {
    web_json_writer Writer = WebJsonBegin(Arena, 0);
    WebJsonBeginObject(Writer);

    WebJsonPutKey(Writer, WEB_SV_LIT("\r\nhello\""));
    WebJsonPutString(Writer, WEB_SV_LIT("\"\b\t\f\\world\""));

    WebJsonEndObject(Writer);
    web_string_view Json = WebJsonEnd(Writer);

    SV_EQUAL(Json, WEB_SV_LIT("{\"\\r\\nhello\\\"\":\"\\\"\\b\\t\\f\\\\world\\\"\"}"));
}

void TestJsonEncoding_Unicode(web_arena *Arena) {
    web_json_writer Writer = WebJsonBegin(Arena, WEB_JSON_ESCAPE_UNICODE);
    WebJsonPutString(Writer, WEB_SV_LIT("аб"));
    SV_EQUAL(WebJsonEnd(Writer), WEB_SV_LIT("\"\\u0430\\u0431\""));
}

void TestJsonDecoding_Escaping(web_arena *Arena) {
    web_string_view Input = WEB_SV_LIT("\"\\\"v\\ra\\nl\\tu\\be\\f\\\\\\\"\"");
    web_json_value Value = {};

    WEB_VERIFY(WebJsonParse(Arena, Input, &Value));
    WEB_VERIFY(Value.Type == JSON_STRING);

    web_string_view S = Value.String;

    SV_EQUAL(S, WEB_SV_LIT("\"v\ra\nl\tu\be\f\\\""));
}

void TestJsonDecoding_Unicode(web_arena *Arena) {
    web_string_view Input = WEB_SV_LIT("\"\\u0430\\u0431\"");
    web_json_value Value = {};

    WEB_VERIFY(WebJsonParse(Arena, Input, &Value));
    WEB_VERIFY(Value.Type == JSON_STRING);

    web_string_view S = Value.String;

    SV_EQUAL(S, WEB_SV_LIT("аб"));

    Input = WEB_SV_LIT("\"\\uD83D\\uDE80\"");

    WEB_VERIFY(WebJsonParse(Arena, Input, &Value));
    WEB_VERIFY(Value.Type == JSON_STRING);

    S = Value.String;

    SV_EQUAL(S, WEB_SV_LIT("🚀"));
}

void TestJsonEncoding(void) {
    web_arena Arena;
    WebArenaInit(&Arena, 676767);

    TestJsonEncoding_StringEscaping(&Arena);
    TestJsonEncoding_Unicode(&Arena);
}

void TestJsonDecoding(void) {
    web_arena Arena;
    WebArenaInit(&Arena, 676767);

    TestJsonDecoding_Escaping(&Arena);
    TestJsonDecoding_Unicode(&Arena);
}

void TestUTF(void) {
    TestUTF8Decoding();
    TestUTF8Encoding();
}

int main() {
    TestUTF();
    TestBase64();
    TestJsonEncoding();
    TestJsonDecoding();
}
