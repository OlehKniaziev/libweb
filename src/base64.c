#include "base64.h"

#define BASE64_TABLE_SIZE 65

static u8 EncodeTable[BASE64_TABLE_SIZE] = {
    [0] = 'A',
    [1] = 'B',
    [2] = 'C',
    [3] = 'D',
    [4] = 'E',
    [5] = 'F',
    [6] = 'G',
    [7] = 'H',
    [8] = 'I',
    [9] = 'J',
    [10] = 'K',
    [11] = 'L',
    [12] = 'M',
    [13] = 'N',
    [14] = 'O',
    [15] = 'P',
    [16] = 'Q',
    [17] = 'R',
    [18] = 'S',
    [19] = 'T',
    [20] = 'U',
    [21] = 'V',
    [22] = 'W',
    [23] = 'X',
    [24] = 'Y',
    [25] = 'Z',
    [26] = 'a',
    [27] = 'b',
    [28] = 'c',
    [29] = 'd',
    [30] = 'e',
    [31] = 'f',
    [32] = 'g',
    [33] = 'h',
    [34] = 'i',
    [35] = 'j',
    [36] = 'k',
    [37] = 'l',
    [38] = 'm',
    [39] = 'n',
    [40] = 'o',
    [41] = 'p',
    [42] = 'q',
    [43] = 'r',
    [44] = 's',
    [45] = 't',
    [46] = 'u',
    [47] = 'v',
    [48] = 'w',
    [49] = 'x',
    [50] = 'y',
    [51] = 'z',
    [52] = '0',
    [53] = '1',
    [54] = '2',
    [55] = '3',
    [56] = '4',
    [57] = '5',
    [58] = '6',
    [59] = '7',
    [60] = '8',
    [61] = '9',
    [62] = '+',
    [63] = '/',
};

static u8 ConvertCharToBits(u8 Char) {
    if (Char >= 'A' && Char <= 'Z') {
        return Char - 'A';
    }
    if (Char >= 'a' && Char <= 'z') {
        return Char - 'a' + 26;
    }
    if (Char >= '0' && Char <= '9') {
        return Char - '0' + 52;
    }
    if (Char == '+') {
        return 62;
    }
    if (Char == '/') {
        return 63;
    }

    return 1 << 7;
}

b32 WebBase64Decode(web_string_view InputBuffer, u8 *OutputBuffer, uz *OutputBufferCount) {
    b32 Result = 1;
    uz OutputCount = 0;

    if (*OutputBufferCount & 3) {
        Result = 0;
        goto End;
    }

    for (uz I = 0; I < InputBuffer.Count; I += 4) {
        u32 Bytes = 0;

        for (uz J = 0; J < 4; ++J) {
            u8 Char = InputBuffer.Items[J + I];
            u8 Bits = ConvertCharToBits(Char);
            if (Bits & (1 << 7)) {
                Result = 0;
                goto End;
            }

            u32 P = Bits << (26 - J * 6);
            Bytes |= P;
        }

        for (uz J = 0; J < 3; ++J) {
            if (OutputCount >= *OutputBufferCount) goto End;

            u8 Shift = 24 - J * 8;
            u32 Mask = 0xFF << Shift;
            u8 OutputByte = (Bytes & Mask) >> Shift;
            OutputBuffer[OutputCount++] = OutputByte;
        }
    }

End:
    *OutputBufferCount = OutputCount;
    return Result;
}

void WebBase64Encode(web_string_view InputBuffer, u8 *OutputBuffer, uz *OutputBufferCount) {
    uz OutputCount = 0;
    uz I;

    for (I = 0; I < InputBuffer.Count; I += 3) {
        u32 Word = (InputBuffer.Items[I] << 16) | (InputBuffer.Items[I + 1] << 8) | (InputBuffer.Items[I + 2]);
        // NOTE(oleh): Hopefully this is unrolled by the compiler.
        for (uz W = 0; W < 4; ++W) {
            if (OutputCount >= *OutputBufferCount) goto End;

            u8 Shift = (18 - W * 6);
            u32 Mask = 0x3F << Shift;
            u8 Bits = (Word & Mask) >> Shift;
            u8 OutputChar = EncodeTable[Bits];
            OutputBuffer[OutputCount++] = OutputChar;
        }
    }

    uz Diff = I - InputBuffer.Count;

    if (Diff == 0) goto End;

    uz LeftoverBytesCount = 3 - Diff;

    if (LeftoverBytesCount == 1) {
        u8 Byte = InputBuffer.Items[InputBuffer.Count - 1];

        if (OutputCount >= *OutputBufferCount) goto End;
        u8 OutputByte = EncodeTable[Byte >> 2];
        OutputBuffer[OutputCount++] = OutputByte;

        if (OutputCount >= *OutputBufferCount) goto End;
        OutputByte = EncodeTable[Byte << 6];
        OutputBuffer[OutputCount++] = OutputByte;

        if (OutputCount >= *OutputBufferCount) goto End;
        OutputBuffer[OutputCount++] = '=';
    } else {
        u16 Bytes = *(u16 *)(InputBuffer.Items + InputBuffer.Count - 2);

        if (OutputCount >= *OutputBufferCount) goto End;
        u8 OutputByte = EncodeTable[Bytes >> 10];
        OutputBuffer[OutputCount++] = OutputByte;

        if (OutputCount >= *OutputBufferCount) goto End;
        OutputByte = EncodeTable[(Bytes >> 4) & 0xFFF];
        OutputBuffer[OutputCount++] = OutputByte;

        if (OutputCount >= *OutputBufferCount) goto End;
        OutputByte = EncodeTable[(Bytes & 0xFF) << 2];
        OutputBuffer[OutputCount++] = OutputByte;

        if (OutputCount >= *OutputBufferCount) goto End;
        OutputBuffer[OutputCount++] = '=';
    }

End:
    *OutputBufferCount = OutputCount;
}
