#include "utf.h"

enum {
    UTF8_2_MASK = 0xC0,
    UTF8_3_MASK = 0xE0,
    UTF8_4_MASK = 0xF0,
};

#ifndef WEB_FAST_UTF8
#    define CHECK_AVAIL(n) if (Avail < (n)) { Stream->Error = 1; return 0; }
#    define CHECK_PREFIX(b) if (((b) & 0xC0) != 0x80) { Stream->Error = 1; return 0; }
#else
#    define CHECK_AVAIL(n)
#    define CHECK_PREFIX(b)
#endif // WEB_FAST_UTF8

b32 WebUTF8StreamNext(web_utf8_stream *Stream, web_code_point *OutCodePoint) {
    if (Stream->Pos >= Stream->View.Count) {
        return 0;
    }

    sz Avail = Stream->View.Count - Stream->Pos;

    u8 *Ptr = &Stream->View.Items[Stream->Pos];

    u8 FirstByte = *Ptr;
    if ((FirstByte & 0xE0) == UTF8_2_MASK) {
        CHECK_AVAIL(2);

        Stream->Pos += 2;

        u8 SecondByte = Ptr[1];
        CHECK_PREFIX(SecondByte);

        if (OutCodePoint == NULL) {
            return 1;
        }

        *OutCodePoint = (web_code_point)(FirstByte & 0x1F)
            | (web_code_point)(SecondByte & 0x3F) << 5;
        return 1;
    }

    if ((FirstByte & 0xF0) == UTF8_3_MASK) {
        CHECK_AVAIL(3);

        Stream->Pos += 3;

        u8 SecondByte = Ptr[1];
        u8 ThirdByte = Ptr[2];
        CHECK_PREFIX(SecondByte);
        CHECK_PREFIX(ThirdByte);

        if (OutCodePoint == NULL) {
            return 1;
        }

        *OutCodePoint = (web_code_point)(FirstByte & 0x0F)
            | (web_code_point)(SecondByte & 0x3F) << 4
            | (web_code_point)(ThirdByte  & 0x3F) << 10;
        return 1;
    }

    if ((FirstByte & 0xF8) == UTF8_4_MASK) {
        CHECK_AVAIL(4);

        Stream->Pos += 4;

        u8 SecondByte = Ptr[1];
        u8 ThirdByte = Ptr[2];
        u8 FourthByte = Ptr[3];
        CHECK_PREFIX(SecondByte);
        CHECK_PREFIX(ThirdByte);
        CHECK_PREFIX(FourthByte);

        if (OutCodePoint == NULL) {
            return 1;
        }

        *OutCodePoint = (web_code_point)(FirstByte & 0x07)
            | (web_code_point)(SecondByte & 0x3F) << 3
            | (web_code_point)(ThirdByte  & 0x3F) << 9
            | (web_code_point)(FourthByte & 0x3F) << 15;
        return 1;
    }

    Stream->Pos += 1;

    if ((FirstByte & 0xC0) != 0) {
        Stream->Error = 1;
        return 0;
    }

    *OutCodePoint = (web_code_point)FirstByte;
    return 1;
}

b32 WebUTF8Encode(
    web_code_point *CodePoints,
    sz CodePointsCount,
    u8 *Output,
    sz OutputCount,
    sz *NumWritten
) {
    b32 Result = 0;
    sz OutputCursor = 0;

    for (sz CodePointIdx = 0; CodePointIdx < CodePointsCount; ++CodePointIdx) {
        web_code_point CodePoint = CodePoints[CodePointIdx];

        if (CodePoint <= WEB_CODE_POINT_ASCII_MAX) {
            if (OutputCursor >= OutputCount) {
                goto End;
            }

            Output[OutputCursor++] = (u8)CodePoint;
        } else if (CodePoint <= 0x07FF) {
            if (OutputCursor + 1 >= OutputCount) {
                goto End;
            }

            Output[OutputCursor++] = (u8)((CodePoint & 0x1F) | UTF8_2_MASK);
            Output[OutputCursor++] = (u8)(((CodePoint >> 5) & 0x3F) | 0x10 << 6);
        } else if (CodePoint <= 0xFFFF) {
            if (OutputCursor + 2 >= OutputCount) {
                goto End;
            }

            Output[OutputCursor++] = (u8)((CodePoint & 0x0F) | UTF8_3_MASK);
            Output[OutputCursor++] = (u8)(((CodePoint >> 4 ) & 0x3F) | 0x10 << 6);
            Output[OutputCursor++] = (u8)(((CodePoint >> 10) & 0x3F) | 0x10 << 6);
        } else {
            if (CodePoint > WEB_CODE_POINT_MAX) {
                goto End;
            }

            if (OutputCursor + 3 >= OutputCount) {
                goto End;
            }

            Output[OutputCursor++] = (u8)((CodePoint & 0x07) | UTF8_4_MASK);
            Output[OutputCursor++] = (u8)(((CodePoint >> 4 ) & 0x3F) | 0x10 << 6);
            Output[OutputCursor++] = (u8)(((CodePoint >> 10) & 0x3F) | 0x10 << 6);
            Output[OutputCursor++] = (u8)(((CodePoint >> 16) & 0x3F) | 0x10 << 6);
        }
    }

    Result = 1;

End:
    if (NumWritten != NULL) *NumWritten = OutputCursor;

    return Result;
}
