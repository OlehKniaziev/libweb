#ifndef COMMON_H_
#define COMMON_H_

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#define WEB_ASSERT(X) do {                                                  \
        if (!(X)) {                                                     \
            fprintf(stderr, "%s:%d: ASSERTION FAILED: %s\n", __FILE__, __LINE__, #X); \
            abort();                                                    \
        }                                                               \
    } while (0)

#define WEB_MEMORY_ZERO(Ptr, Size) (memset((Ptr), 0, (Size)))

#define WEB_STRUCT_ZERO(Ptr) MEMORY_ZERO((Ptr), sizeof(*(Ptr)))

#define WEB_PANIC(Msg) do {                                                 \
        fprintf(stderr, "%s:%d: PROGRAM PANICKED: %s\n", __FILE__, __LINE__, Msg); \
        abort();                                                    \
    } while (0)

#define WEB_PANIC_FMT(Fmt, ...) do {                                        \
        fprintf(stderr, "%s:%d: PROGRAM PANICKED: " Fmt "\n", __FILE__, __LINE__, __VA_ARGS__); \
        abort();                                                    \
    } while (0)

#define WEB_SV_FMT "%.*s"
#define WEB_SV_ARG(Sv) (int)(Sv).Count, (const char *)(Sv).Items
#define WEB_SV_LIT(Lit) ((web_string_view){.Items = (u8 *)(Lit), .Count = strlen(Lit)})

#define WEB_UNREACHABLE() WEB_PANIC("Encountered unreachable code!")

#define WEB_TODO_MSG(Msg) do {                                        \
        fprintf(stderr, "%s:%d: TODO: " Msg "\n", __FILE__, __LINE__); \
        abort();                                                  \
    } while (0)

#define WEB_TODO() WEB_TODO_MSG("NOT IMPLEMENTED")

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

typedef u8 b8;
typedef u16 b16;
typedef u32 b32;
typedef u64 b64;

typedef float f32;
typedef double f64;

typedef size_t uz;
typedef ssize_t sz;

typedef struct {
    u8 *Items;
    uz Count;
} web_string_view;

static inline b32 WebStringViewEqualCStr(web_string_view Sv, const char *CStr) {
    uz CStrLength = strlen(CStr);
    if (Sv.Count != CStrLength) return 0;

    for (uz I = 0; I < Sv.Count; ++I) {
        if (Sv.Items[I] != CStr[I]) return 0;
    }

    return 1;
}

static inline b32 WebStringViewEqual(web_string_view Lhs, web_string_view Rhs) {
    if (Lhs.Count != Rhs.Count) return 0;

    for (uz I = 0; I < Lhs.Count; ++I) {
        if (Lhs.Items[I] != Rhs.Items[I]) return 0;
    }

    return 1;
}

typedef struct {
    u8 *Items;
    void *LastAlloc;
    uz Capacity;
    uz Offset;
} web_arena;

static inline uz AlignForward(uz Size, uz Alignment) {
    return Size + ((Alignment - (Size & (Alignment - 1))) & (Alignment - 1));
}

static inline void *ArenaPush(web_arena *Arena, uz Size) {
    Size = AlignForward(Size, sizeof(uz));
    uz AvailableBytes = Arena->Capacity - Arena->Offset;
    if (AvailableBytes < Size) WEB_PANIC_FMT("Arena out of memory for requested size %zu!", Size);

    void *Ptr = Arena->Items + Arena->Offset;
    Arena->Offset += Size;
    Arena->LastAlloc = Ptr;
    return Ptr;
}

#define WEB_ARENA_PUSH_ZERO(Arena, Size) (WEB_MEMORY_ZERO(ArenaPush((Arena), (Size)), (Size)))

static inline void WebArenaInit(web_arena *Arena, uz Capacity) {
    Arena->Capacity = Capacity;
    Arena->Offset = 0;
    Arena->Items = (u8 *)calloc(Capacity, sizeof(u8));
    Arena->LastAlloc = NULL;
}

static inline web_string_view WebArenaFormat(web_arena *Arena, const char *Fmt, ...) {
    va_list Args;
    va_start(Args, Fmt);
    uz BytesNeeded = vsnprintf(NULL, 0, Fmt, Args);
    ++BytesNeeded; // NOTE(oleh): Null terminator.
    va_end(Args);

    u8 *Buffer = ArenaPush(Arena, BytesNeeded);
    va_start(Args, Fmt);
    vsprintf((char *)Buffer, Fmt, Args);
    va_end(Args);

    return (web_string_view) {.Items = Buffer, .Count = BytesNeeded - 1};
}

static inline void *WebArenaRealloc(web_arena *Arena, void *OldPtr, uz OldSize, uz NewSize) {
    if (Arena->LastAlloc == OldPtr) {
        OldSize = AlignForward(OldSize, sizeof(uz));
        NewSize = AlignForward(NewSize, sizeof(uz));
        sz Diff = (sz)NewSize - (sz)OldSize;
        Arena->Offset += Diff;
    }
    void* NewPtr = ArenaPush(Arena, NewSize);
    memcpy(NewPtr, OldPtr, OldSize);
    return NewPtr;
}

void WebArenaPop(web_arena *, uz);

static inline void WebArenaReset(web_arena *Arena) {
    Arena->Offset = 0;
}

web_arena *WebGetTempArena(void);

#define WEB_ARENA_NEW(Arena, Type) ((Type *)WEB_MEMORY_ZERO(ArenaPush((Arena), sizeof(Type)), sizeof(Type)))

static inline char *WebStringViewCloneCStr(web_arena *Arena, web_string_view Sv) {
    char *Buffer = ArenaPush(Arena, Sv.Count + 1);
    memcpy(Buffer, Sv.Items, Sv.Count);
    Buffer[Sv.Count] = '\0';
    return Buffer;
}

b32 WebReadFullFile(web_arena *Arena, const char *Path, web_string_view *OutContents);

#define WEB_DEFAULT_ARRAY_CAPACITY 7

#define WEB_ARRAY_INIT(Arena, Array) do {                                   \
        (Array)->Capacity = WEB_DEFAULT_ARRAY_CAPACITY;                     \
        (Array)->Count = 0;                                             \
        (Array)->Items = WEB_ARENA_PUSH_ZERO((Arena), sizeof(*(Array)->Items) * WEB_DEFAULT_ARRAY_CAPACITY); \
    } while (0)

#define WEB_ARRAY_PUSH(Arena, Array, Element) do {                          \
        if ((Array)->Count >= (Array)->Capacity) {                      \
            uz NewCapacity = ((Array)->Capacity + 1) * 2;               \
            (Array)->Items = WebArenaRealloc((Arena), (Array)->Items, sizeof(*(Array)->Items) * (Array)->Capacity, sizeof(*(Array)->Items) * NewCapacity); \
            (Array)->Capacity = NewCapacity;                            \
        }                                                               \
                                                                        \
        (Array)->Items[(Array)->Count] = Element;                       \
        ++(Array)->Count;                                               \
    } while (0)

u64 WebHashFnv1(web_string_view Input);

typedef struct {
    b8 HasValue;
    web_string_view Value;
} optional_web_string_view;

typedef struct {
    b8 HasValue;
    f64 Value;
} optional_f64;

typedef struct {
    b8 HasValue;
    u64 Value;
} optional_u64;

typedef struct {
    b8 HasValue;
    u32 Value;
} optional_u32;

#ifdef __cplusplus
}
#endif

#endif // COMMON_H_
