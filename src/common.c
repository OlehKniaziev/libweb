#include "common.h"

#include <ctype.h>
#include <fcntl.h>

static __thread web_arena TempArena;

#define TEMP_ARENA_CAPACITY (4l * 1024l * 1024l)

web_arena *WebGetTempArena(void) {
    if (TempArena.Items == NULL) {
        WebArenaInit(&TempArena, TEMP_ARENA_CAPACITY);
    } else {
        WebArenaReset(&TempArena);
    }

    return &TempArena;
}

b32 WebReadFullFile(web_arena *Arena, const char *Path, web_string_view *OutContents) {
    int Fd = open(Path, O_RDONLY);
    if (Fd == -1) return 0;

    off_t FileSize = lseek(Fd, 0, SEEK_END);
    lseek(Fd, 0, SEEK_SET);

    u8 *ContentsBuffer = WebArenaPush(Arena, FileSize);
    ssize_t BytesRead = read(Fd, ContentsBuffer, FileSize);
    if (BytesRead == -1) return 0;

    // FIXME(oleh): Check for an error (maybe).
    close(Fd);

    OutContents->Items = ContentsBuffer;
    OutContents->Count = FileSize;
    return 1;
}

u64 WebHashFnv1(web_string_view Input) {
    const u64 FnvOffsetBasis = 0xCBF29CE484222325;
    const u64 FnvPrime = 0x100000001B3;

    u64 Hash = FnvOffsetBasis;

    for (uz I = 0; I < Input.Count; ++I) {
        Hash *= FnvPrime;

        u8 Byte = Input.Items[I];
        Hash ^= (u64)Byte;
    }

    return Hash;
}

b32 WebParseS64(web_string_view Buffer, s64 *Out) {
    if (Buffer.Count == 0) return 0;

    s64 Result = 0;
    uz Coef = 1;

    for (sz I = Buffer.Count - 1; I > 0; --I) {
        u8 Char = Buffer.Items[I];
        if (!isdigit(Char)) return 0;

        u8 Digit = Char - '0';
        Result += Digit * Coef;
        Coef *= 10;
    }

    if (isdigit(Buffer.Items[0])) Result += (Buffer.Items[0] - '0') * Coef;
    else if (Buffer.Items[0] == '-') Result = -Result;
    else return 0;

    *Out = Result;

    return 1;
}
