#ifndef BASE64_H_
#define BASE64_H_

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

b32 WebBase64Decode(web_string_view InputBuffer, u8 *OutputBuffer, uz *OutputBufferCount);
void WebBase64Encode(web_string_view InputBuffer, u8 *OutputBuffer, uz *OutputBufferCount);

#ifdef __cplusplus
}
#endif

#endif // BASE64_H_
