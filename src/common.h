#ifndef WEB_COMMON_H_
#define WEB_COMMON_H_

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdalign.h>

#define _WEB_DO_ASSERT(Msg, X) do {                                                  \
        if (!(X)) {                                                     \
            fprintf(stderr, "%s:%d: " Msg ": %s\n", __FILE__, __LINE__, #X); \
            abort();                                                    \
        }                                                               \
} while (0)

#define WEB_VERIFY(X) _WEB_DO_ASSERT("VERIFICATION FAILED", (X))

#ifndef WEB_STRIP_ASSERTS
#    define WEB_ASSERT(X) _WEB_DO_ASSERT("ASSERTION FAILED", (X))
#else
#    define WEB_ASSERT(X)
#endif // WEB_STRIP_ASSERTS

#define WEB_MEMORY_ZERO(Ptr, Size) (memset((Ptr), 0, (Size)))

#define WEB_STRUCT_ZERO(Ptr) WEB_MEMORY_ZERO((Ptr), sizeof(*(Ptr)))

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

#define WEB_MIN(a, b) ((a) < (b) ? (a) : (b))
#define WEB_MAX(a, b) ((a) > (b) ? (a) : (b))

#define WEB_ARRAY_COUNT(Arr) (sizeof((Arr)) / (sizeof((Arr)[0])))

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
    sz Count;
} web_string_view;

typedef struct {
    u8 *Items;
    sz Count;
    sz Capacity;
} web_dynamic_string;

static inline b32 WebStringViewEqualCStr(web_string_view Sv, const char *CStr) {
    sz CStrLength = strlen(CStr);
    if (Sv.Count != CStrLength) return 0;

    for (sz I = 0; I < Sv.Count; ++I) {
        if (Sv.Items[I] != CStr[I]) return 0;
    }

    return 1;
}

static inline b32 WebStringViewEqual(web_string_view Lhs, web_string_view Rhs) {
    if (Lhs.Count != Rhs.Count) return 0;

    for (sz I = 0; I < Lhs.Count; ++I) {
        if (Lhs.Items[I] != Rhs.Items[I]) return 0;
    }

    return 1;
}

typedef struct {
    u8 *Items;
    void *LastAlloc;
    uz Capacity;
    uz Offset;
} web_fixed_arena;

typedef struct web_chained_arena_block {
    struct web_chained_arena_block *Next;
    u8 *Items;
    void *LastAlloc;
    uz Capacity;
    uz Offset;
} web_chained_arena_block;

typedef struct {
    web_chained_arena_block *Head;
    web_chained_arena_block *LastAllocBlock;
} web_chained_arena;

typedef struct {
    b32 Chained;
    union {
        web_fixed_arena Fixed;
        web_chained_arena Chained;
    } U;
} web_arena;

static inline uz WebAlignForward(uz Size, uz Alignment) {
    WEB_VERIFY(Alignment == 1 || (Alignment & 1) == 0);
    return Size + ((Alignment - (Size & (Alignment - 1))) & (Alignment - 1));
}

void *WebArenaPush(web_arena *Arena, uz Size, uz Align);

#define WEB_ARENA_PUSH_ZERO(Arena, Size, Align) (WEB_MEMORY_ZERO(WebArenaPush((Arena), (Size), (Align)), WebAlignForward((Size), (Align))))

void WebArenaInitFixed(web_arena *Arena, uz Capacity);
void WebArenaInitChained(web_arena *Arena, uz Capacity);

#ifdef __GNUC__
#    define WEB_ATTRIBUTE_PRINTF(Fmt, Args) __attribute__((format(printf, Fmt, Args)))
#else
#    define WEB_ATTRIBUTE_PRINTF(Fmt, Args)
#endif // __GNUC__

web_string_view WebArenaFormat(web_arena *Arena, const char *Fmt, ...) WEB_ATTRIBUTE_PRINTF(2, 3);

void *WebArenaRealloc(web_arena *Arena, void *OldPtr, uz OldSize, uz NewSize, uz Align);

void WebArenaPop(web_arena *Arena, uz Size);

void WebArenaReset(web_arena *Arena);

typedef struct {
    web_arena Arena;
} web_temp;

web_temp WebGetTempArena(void);
void WebReturnTempArena(web_temp Temp);

#define WEB_ARENA_NEW(Arena, Type) ((Type *)WEB_MEMORY_ZERO(WebArenaPush((Arena), sizeof(Type), alignof(Type)), sizeof(Type)))
#define WEB_ARENA_NEW_MANY(Arena, Type, Count) ((Type *)WEB_MEMORY_ZERO(WebArenaPush((Arena), sizeof(Type) * (Count), alignof(Type)), sizeof(Type) * (Count)))

#define WEB_ARENA_REALLOC_ITEMS(Arena, Items, OldCount, NewCount) (WebArenaRealloc((Arena), (Items), sizeof(*(Items)) * (OldCount), sizeof(*Items) * (NewCount), alignof(*(Items))))

static inline char *WebStringViewCloneCStr(web_arena *Arena, web_string_view Sv) {
    char *Buffer = (char *)WebArenaPush(Arena, Sv.Count + 1, sizeof(char));
    memcpy(Buffer, Sv.Items, Sv.Count);
    Buffer[Sv.Count] = '\0';
    return Buffer;
}

b32 WebReadFullFile(web_arena *Arena, const char *Path, web_string_view *OutContents);

#define WEB_DEFAULT_ARRAY_CAPACITY 7

#define WEB_ARRAY_INIT(Arena, Array) do {                                   \
        (Array)->Capacity = WEB_DEFAULT_ARRAY_CAPACITY;                     \
        (Array)->Count = 0;                                             \
        (Array)->Items = WEB_ARENA_PUSH_ZERO((Arena), sizeof(*(Array)->Items) * WEB_DEFAULT_ARRAY_CAPACITY, alignof(*(Array)->Items)); \
    } while (0)

#define WEB_ARRAY_PUSH(Arena, Array, Element) do {                          \
        if ((Array)->Count >= (Array)->Capacity) {                      \
            sz NewCapacity = ((Array)->Capacity + 1) * 2;               \
            (Array)->Items = WebArenaRealloc((Arena), (Array)->Items, sizeof(*(Array)->Items) * (Array)->Capacity, sizeof(*(Array)->Items) * NewCapacity, alignof(*(Array)->Items)); \
            (Array)->Capacity = NewCapacity;                            \
        }                                                               \
                                                                        \
        (Array)->Items[(Array)->Count] = Element;                       \
        ++(Array)->Count;                                               \
    } while (0)

// TODO(oleh): Since we know the number of elements in the Rhs array, we can first reserve an
// exact amount of memory needed for insertion of all of them.
#define WEB_ARRAY_EXTEND(Arena, Lhs, Rhs) do { \
    for (sz I = 0; I < (Rhs)->Count; ++I) { \
    WEB_ARRAY_PUSH((Arena), (Lhs), (Rhs)->Items[I]); \
    } \
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

b32 WebParseS64(web_string_view, s64 *);

#ifdef __cplusplus
}
#endif

#endif // WEB_COMMON_H_
