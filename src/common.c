#include "common.h"
#include <fcntl.h>

static arena TempArena;

#define TEMP_ARENA_CAPACITY (4l * 1024l * 1024l)

arena *GetTempArena(void) {
    if (TempArena.Items == NULL) {
        ArenaInit(&TempArena, TEMP_ARENA_CAPACITY);
    } else {
        ArenaReset(&TempArena);
    }

    return &TempArena;
}

b32 ReadFullFile(arena *Arena, const char *Path, string_view *OutContents) {
    int Fd = open(Path, O_RDONLY);
    if (Fd == -1) return 0;

    off_t FileSize = lseek(Fd, 0, SEEK_END);
    lseek(Fd, 0, SEEK_SET);

    u8 *ContentsBuffer = ArenaPush(Arena, FileSize);
    ssize_t BytesRead = read(Fd, ContentsBuffer, FileSize);
    if (BytesRead == -1) return 0;

    // FIXME(oleh): Check for an error (maybe).
    close(Fd);

    OutContents->Items = ContentsBuffer;
    OutContents->Count = FileSize;
    return 1;
}

u64 HashFnv1(string_view Input) {
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
