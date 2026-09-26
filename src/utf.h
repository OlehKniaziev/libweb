#ifndef WEB_UTF_H_
#define WEB_UTF_H_

#include "common.h"

typedef u32 web_code_point;

#define WEB_CODE_POINT_ASCII_MAX ((web_code_point)127)
#define WEB_CODE_POINT_MAX ((web_code_point)0x0010FFFFF)

typedef struct {
    web_string_view View;
    sz Pos;
    b32 Error;
} web_utf8_stream;

b32 WebUTF8StreamNext(
    web_utf8_stream *Stream,
    web_code_point *CodePoint
);

b32 WebUTF8Encode(
    web_code_point *CodePoints,
    sz CodePointsCount,
    u8 *Output,
    sz OutputCount,
    sz *NumWritten
);

#endif // WEB_UTF_H_
