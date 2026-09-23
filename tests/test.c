#include "../src/base64.h"
#include "../src/json.h"

#define SV_EQUAL(Lhs, Rhs) do { \
if (!WebStringViewEqual((Lhs), (Rhs))) WEB_PANIC_FMT("Assertion failed: '" WEB_SV_FMT "' != '" WEB_SV_FMT "'", WEB_SV_ARG((Lhs)), WEB_SV_ARG((Rhs))); \
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

void TestJsonEncoding_StringEscaping(web_arena *Arena) {
    web_json_writer Writer = WebJsonBegin(Arena);
    WebJsonBeginObject(Writer);

    WebJsonPutKey(Writer, WEB_SV_LIT("\r\nhello\""));
    WebJsonPutString(Writer, WEB_SV_LIT("\"\b\t\f\\world\""));

    WebJsonEndObject(Writer);
    web_string_view Json = WebJsonEnd(Writer);

    SV_EQUAL(Json, WEB_SV_LIT("{\"\\r\\nhello\\\"\":\"\\\"\\b\\t\\f\\\\world\\\"\"}"));
}

void TestJsonDecoding_Escaping(web_arena *Arena) {
    web_string_view Input = WEB_SV_LIT("\"\\\"v\\ra\\nl\\tu\\be\\f\\\\\\\"\"");
    web_json_value Value = {};

    WEB_VERIFY(WebJsonParse(Arena, Input, &Value));
    WEB_VERIFY(Value.Type == JSON_STRING);

    web_string_view S = Value.String;

    SV_EQUAL(S, WEB_SV_LIT("\"v\ra\nl\tu\be\f\\\""));
}

void TestJsonEncoding(void) {
    web_arena Arena;
    WebArenaInit(&Arena, 676767);

    TestJsonEncoding_StringEscaping(&Arena);
}

void TestJsonDecoding(void) {
    web_arena Arena;
    WebArenaInit(&Arena, 676767);

    TestJsonDecoding_Escaping(&Arena);
}

int main() {
    TestBase64();
    TestJsonEncoding();
    TestJsonDecoding();
}
