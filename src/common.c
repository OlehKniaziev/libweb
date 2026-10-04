#include "common.h"
#include "test.h"

#include <ctype.h>
#include <fcntl.h>

static void FixedArenaInit(web_fixed_arena *Arena, uz Capacity) {
    Arena->Capacity = Capacity;
    Arena->Offset = 0;
    Arena->Items = (u8 *)calloc(Capacity, sizeof(u8));
    Arena->LastAlloc = NULL;
}

void WebArenaInitFixed(web_arena *Arena, uz Capacity) {
    FixedArenaInit(&Arena->U.Fixed, Capacity);
    Arena->Chained = 0;
}

static web_chained_arena_block *ChainedArenaBlockAlloc(uz Size, uz Align) {
    Size = WebAlignForward(Size, Align);
    uz BlockSize = WebAlignForward(sizeof(web_chained_arena_block), Align);
    void *Mem = calloc(BlockSize + Size, 1);

    web_chained_arena_block *Block = Mem;
    Block->Items = Mem + BlockSize;
    Block->Capacity = Size;

    return Block;
}

static void ChainedArenaInit(web_chained_arena *Arena, uz Capacity) {
    Arena->Head = ChainedArenaBlockAlloc(Capacity, 1);
    Arena->LastAllocBlock = NULL;
}

void WebArenaInitChained(web_arena *Arena, uz Capacity) {
    ChainedArenaInit(&Arena->U.Chained, Capacity);
    Arena->Chained = 1;
}

static __thread web_fixed_arena TempArena;

#define TEMP_ARENA_CAPACITY (4l * 1024l * 1024l)
#define TEMP_ARENA_CHUNK_CAPACITY (TEMP_ARENA_CAPACITY / 32l)

web_temp WebGetTempArena(void) {
    if (TempArena.Items == NULL) {
        FixedArenaInit(&TempArena, TEMP_ARENA_CAPACITY);
    }

    web_fixed_arena Fixed = {
        .Items = TempArena.Items + TempArena.Offset,
        .Capacity = TEMP_ARENA_CHUNK_CAPACITY,
    };

    TempArena.Offset += Fixed.Capacity;

    WEB_VERIFY(TempArena.Offset <= TempArena.Capacity);

    return (web_temp){.Arena = {
        .U.Fixed = Fixed,
    }};
}

void WebReturnTempArena(web_temp Temp) {
    WEB_ASSERT(!Temp.Arena.Chained);
    WEB_ASSERT(Temp.Arena.U.Fixed.Items == TempArena.Items + TempArena.Offset - Temp.Arena.U.Fixed.Capacity);
    TempArena.Offset -= Temp.Arena.U.Fixed.Capacity;
}

b32 WebReadFullFile(web_arena *Arena, const char *Path, web_string_view *OutContents) {
    int Fd = open(Path, O_RDONLY);
    if (Fd == -1) return 0;

    off_t FileSize = lseek(Fd, 0, SEEK_END);
    lseek(Fd, 0, SEEK_SET);

    u8 *ContentsBuffer = WebArenaPush(Arena, FileSize, 1);
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

    for (sz I = 0; I < Input.Count; ++I) {
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

#ifndef WEB_MEMORY_SANITIZER
static void *FixedArenaPush(web_fixed_arena *Fixed, uz Size, uz Align) {
    Size = WebAlignForward(Size, Align);
    uz AvailableBytes = Fixed->Capacity - Fixed->Offset;
    if (AvailableBytes < Size) WEB_PANIC_FMT("Fixed arena out of memory for requested size %zu!", Size);

    void *Ptr = Fixed->Items + Fixed->Offset;
    Fixed->Offset += Size;
    Fixed->LastAlloc = Ptr;
    return Ptr;
}

static void *ChainedArenaPush(web_chained_arena *Arena, uz Size, uz Align) {
    Size = WebAlignForward(Size, Align);

    web_chained_arena_block *Block = Arena->Head;
    while (Block != NULL) {
        uz BlockAlignedOffset = WebAlignForward(Block->Offset, Align);
        uz Avail = Block->Capacity - BlockAlignedOffset;
        if (Avail >= Size) break;

        Block = Block->Next;
    }

    if (Block == NULL) {
        Block = ChainedArenaBlockAlloc(Size, Align);
        Block->Next = Arena->Head;
        Arena->Head = Block;
    }

    Block->Offset = WebAlignForward(Block->Offset, Align);
    void *Ptr = Block->Items + Block->Offset;
    Block->Offset += Size;

    Block->LastAlloc = Ptr;
    Arena->LastAllocBlock = Block;

    return Ptr;
}

static void *FixedArenaRealloc(web_fixed_arena *Arena, void *OldPtr, uz OldSize, uz NewSize, uz Align) {
    if (Arena->LastAlloc == OldPtr) {
        OldSize = WebAlignForward(OldSize, sizeof(uz));
        NewSize = WebAlignForward(NewSize, sizeof(uz));
        sz Diff = (sz)NewSize - (sz)OldSize;
        Arena->Offset += Diff;
    }
    void* NewPtr = FixedArenaPush(Arena, NewSize, Align);
    memcpy(NewPtr, OldPtr, OldSize);
    return NewPtr;
}

static void *ChainedArenaRealloc(web_chained_arena *Arena, void *OldPtr, uz OldSize, uz NewSize, uz Align) {
    NewSize = WebAlignForward(NewSize, Align);

    web_chained_arena_block *Block = Arena->Head;
    while (Block != NULL) {
        if (Block->LastAlloc == OldPtr) {
            uz BlockAlignedOffset = WebAlignForward(Block->Offset, Align);
            void *AlignedPtr = (void *)WebAlignForward((uz)OldPtr, Align);
            uz AlignmentDelta = (uz)AlignedPtr - (uz)OldPtr;
            BlockAlignedOffset += AlignmentDelta;

            if (BlockAlignedOffset <= Block->Capacity && Block->Capacity - BlockAlignedOffset >= NewSize) {
                Block->Offset = BlockAlignedOffset + NewSize;
                return AlignedPtr;
            }

            break;
        }

        Block = Block->Next;
    }

    void *Ptr = ChainedArenaPush(Arena, NewSize, Align);
    memcpy(Ptr, OldPtr, OldSize);
    return Ptr;
}

#else
void *FixedArenaPush(web_fixed_arena *Arena, uz Size, uz Align) {
    (void) Arena;
    Size = WebAlignForward(Size, Align);
    return malloc(Size);
}

void *ChainedArenaPush(web_chained_arena *Arena, uz Size, uz Align) {
    (void) Arena;
    Size = WebAlignForward(Size, Align);
    return malloc(Size);
}

static void *ChainedArenaRealloc(web_chained_arena *Arena, void *OldPtr, uz OldSize, uz NewSize, uz Align) {
    (void) Arena;
    (void) OldSize;
    NewSize = WebAlignForward(NewSize, Align);
    return realloc(OldPtr, NewSize);
}

static void *FixedArenaRealloc(web_fixed_arena *Arena, void *OldPtr, uz OldSize, uz NewSize, uz Align) {
    (void) Arena;
    (void) OldSize;
    NewSize = WebAlignForward(NewSize, Align);
    return realloc(OldPtr, NewSize);
}
#endif // WEB_MEMORY_SANITIZER

void *WebArenaPush(web_arena *Arena, uz Size, uz Align) {
    if (Arena->Chained) {
        return ChainedArenaPush(&Arena->U.Chained, Size, Align);
    } else {
        return FixedArenaPush(&Arena->U.Fixed, Size, Align);
    }
}

void *WebArenaRealloc(web_arena *Arena, void *OldPtr, uz OldSize, uz NewSize, uz Align) {
    if (Arena->Chained) {
        return ChainedArenaRealloc(&Arena->U.Chained, OldPtr, OldSize, NewSize, Align);
    } else {
        return FixedArenaRealloc(&Arena->U.Fixed, OldPtr, OldSize, NewSize, Align);
    }
}

static void ChainedArenaPop(web_chained_arena *Arena, uz Size) {
    web_chained_arena_block *Block = Arena->LastAllocBlock;

    if (Size > Block->Offset) {
        Block->Offset = 0;
    } else {
        Block->Offset -= Size;
    }
}

static void FixedArenaPop(web_fixed_arena *Arena, uz Size) {
    if (Size > Arena->Offset) {
        Arena->Offset = 0;
    } else {
        Arena->Offset -= Size;
    }
}

void WebArenaPop(web_arena *Arena, uz Size) {
    if (Arena->Chained) {
        ChainedArenaPop(&Arena->U.Chained, Size);
    } else {
        FixedArenaPop(&Arena->U.Fixed, Size);
    }
}

static void ChainedArenaReset(web_chained_arena *Arena) {
    web_chained_arena_block *Block = Arena->Head;
    while (Block != NULL) {
        Block->Offset = 0;
        Block = Block->Next;
    }
}

static void FixedArenaReset(web_fixed_arena *Arena) {
    Arena->Offset = 0;
}

void WebArenaReset(web_arena *Arena) {
    if (Arena->Chained) {
        ChainedArenaReset(&Arena->U.Chained);
    } else {
        FixedArenaReset(&Arena->U.Fixed);
    }
}

web_string_view WebArenaFormat(web_arena *Arena, const char *Fmt, ...) {
    va_list Args;
    va_start(Args, Fmt);
    uz ResultCount = vsnprintf(NULL, 0, Fmt, Args);
    va_end(Args);

    u8 *Buffer = (u8 *)WebArenaPush(Arena, ResultCount + 1, 1);
    va_start(Args, Fmt);
    vsprintf((char *)Buffer, Fmt, Args);
    va_end(Args);

    web_string_view Result;
    Result.Items = Buffer;
    Result.Count = ResultCount;
    return Result;
}

web_string_view WebStringViewChop(web_string_view Sv, web_string_view Delimiter) {
    if (Delimiter.Count > Sv.Count) {
        return Sv;
    }

    for (sz CharIdx = 0; CharIdx <= Sv.Count - Delimiter.Count; ++CharIdx) {
        web_string_view Chunk = {.Items = Sv.Items + CharIdx, .Count = Delimiter.Count};
        if (WebStringViewEqual(Chunk, Delimiter)) {
            return (web_string_view){
                .Items = Sv.Items,
                .Count = CharIdx,
            };
        }
    }

    return Sv;
}

web_string_view WebStringViewChopCStr(web_string_view Sv, const char *Delimiter) {
    return WebStringViewChop(Sv, WEB_SV_LIT(Delimiter));
}

typedef WEB_MAP_TYPE(web_string_view, s32) string_map;

WEB_DEFINE_TEST(MapOps) {
    string_map Map = {0};
    WEB_STRING_MAP_INIT(&Runner->Arena, &Map);

    web_string_view Key = WEB_SV_LIT("hello");
    s32 Value = 1;

    WEB_MAP_INSERT(&Runner->Arena, &Map, Key, Value);
    WEB_T_EQUAL(Map.Count, 1);

    s32 GetValue = 1;
    WEB_T_TRUE(WEB_MAP_GET(&Map, Key, &GetValue));
    WEB_T_EQUAL(GetValue, Value);

    WEB_MAP_INSERT(&Runner->Arena, &Map, Key, Value + 1);
    WEB_T_EQUAL(Map.Count, 1);

    WEB_T_TRUE(WEB_MAP_GET(&Map, Key, &GetValue));
    WEB_T_EQUAL(GetValue, Value + 1);
}

// #if 0
WEB_DEFINE_TEST(MapGrow) {
    string_map Map = {0};
    WEB_STRING_MAP_INIT(&Runner->Arena, &Map);

    WEB_T_EQUAL(Map.Capacity, WEB_MAP_DEFAULT_CAP);

    WEB_ARRAY_TYPE(web_string_view) Keys = {0};
    WEB_ARRAY_INIT(&Runner->Arena, &Keys);

    WEB_ARRAY_TYPE(s32) Values = {0};
    WEB_ARRAY_INIT(&Runner->Arena, &Values);

    for (s32 Value = 0; Value < WEB_MAP_DEFAULT_CAP; ++Value) {
        web_string_view Key = WebArenaFormat(&Runner->Arena, "%d", Value);
        WEB_MAP_INSERT(&Runner->Arena, &Map, Key, Value);

        WEB_ARRAY_PUSH(&Runner->Arena, &Keys, Key);
        WEB_ARRAY_PUSH(&Runner->Arena, &Values, Value);
    }

    WEB_T_EQUAL(Map.Count, Keys.Count);
    WEB_T_EQUAL(Map.Capacity, WEB_MAP_CAPACITY_STEP(WEB_MAP_DEFAULT_CAP));

    WEB_T_EQUAL(Keys.Count, Values.Count);

    for (sz KeyIdx = 0; KeyIdx < Keys.Count; ++KeyIdx) {
        web_string_view Key = Keys.Items[KeyIdx];
        s32 ExpectedValue = Values.Items[KeyIdx];

        s32 GotValue = 0;
        WEB_T_TRUE(WEB_MAP_GET(&Map, Key, &GotValue));
        WEB_T_EQUAL(ExpectedValue, GotValue);
    }
}
// #endif // 0
