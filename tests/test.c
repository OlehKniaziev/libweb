#include "../src/base64.h"

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

int main() {
    TestBase64();
}
