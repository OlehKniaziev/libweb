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

#define WEB_PANIC_FMT_SOURCE(Source, Fmt, ...) do {                                        \
        fprintf(stderr, "%s:%d: PROGRAM PANICKED: " Fmt "\n", (Source).FileName, (Source).Line, __VA_ARGS__); \
        abort();                                                    \
    } while (0)

#define WEB_PANIC_FMT(Fmt, ...) WEB_PANIC_FMT_SOURCE(WEB_SOURCE_INFO_GET(), Fmt, __VA_ARGS__)
#define WEB_PANIC(Msg) WEB_PANIC_FMT("%s", (Msg))

#define WEB_SV_FMT "%.*s"
#define WEB_SV_ARG(Sv) (int)(Sv).Count, (const char *)(Sv).Items
#define WEB_SV_LIT(Lit) ((web_string_view){.Items = (u8 *)(Lit), .Count = (sz)strlen(Lit)})

#define WEB_UNREACHABLE() WEB_PANIC("Encountered unreachable code!")

#define WEB_TODO_MSG(Msg) do {                                        \
        fprintf(stderr, "%s:%d: TODO: " Msg "\n", __FILE__, __LINE__); \
        abort();                                                  \
    } while (0)

#define WEB_TODO() WEB_TODO_MSG("NOT IMPLEMENTED")

#define WEB_MIN(a, b) ((a) < (b) ? (a) : (b))
#define WEB_MAX(a, b) ((a) > (b) ? (a) : (b))

#define WEB_ARRAY_COUNT(Arr) ((sz)(sizeof((Arr)) / (sizeof((Arr)[0]))))

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

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
    const char *FileName;
    const char *ProcName;
    u32 Line;
} web_source_info;

#define WEB_SOURCE_INFO_GET() ((web_source_info) {.FileName = __FILE__, .ProcName = __FUNCTION__, .Line = __LINE__})

typedef struct {
    u8 *Items;
    sz Count;
} web_string_view;

typedef struct {
    u8 *Items;
    sz Count;
    sz Capacity;
} web_dynamic_string;

static inline s32 WebStringViewCompare(web_string_view Lhs, web_string_view Rhs) {
    sz D = Lhs.Count - Rhs.Count;
    if (D != 0) return D;
    return memcmp(Lhs.Items, Rhs.Items, Lhs.Count);
}

static inline b32 WebStringViewEqual(web_string_view Lhs, web_string_view Rhs) {
    return WebStringViewCompare(Lhs, Rhs) == 0;
}

static inline b32 WebStringViewEqualCStr(web_string_view Sv, const char *CStr) {
    return WebStringViewEqual(Sv, WEB_SV_LIT(CStr));
}

static inline b32 WebStringViewHasPrefix(web_string_view Sv, web_string_view Prefix) {
    if (Prefix.Count == 0) return 1;
    if (Sv.Count == 0 || Prefix.Count > Sv.Count) return 0;
    return memcmp(Sv.Items, Prefix.Items, Prefix.Count) == 0;
}

static inline b32 WebStringViewHasPrefixCStr(web_string_view Sv, const char *Prefix) {
    return WebStringViewHasPrefix(Sv, WEB_SV_LIT(Prefix));
}

static inline b32 WebStringViewHasSuffix(web_string_view Sv, web_string_view Suffix) {
    if (Suffix.Count == 0) return 1;
    if (Sv.Count == 0 || Suffix.Count > Sv.Count) return 0;
    return memcmp(Sv.Items + Sv.Count - Suffix.Count, Suffix.Items, Suffix.Count) == 0;
}

static inline b32 WebStringViewHasSuffixCStr(web_string_view Sv, const char *Suffix) {
    return WebStringViewHasSuffix(Sv, WEB_SV_LIT(Suffix));
}

web_string_view WebStringViewChop(web_string_view Sv, web_string_view Delimiter);
web_string_view WebStringViewChopCStr(web_string_view Sv, const char *Delimiter);

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
#    define WEB_ATTRIBUTE_MAYBE_UNUSED __attribute__((unused))
#else
#    define WEB_ATTRIBUTE_PRINTF(Fmt, Args)
#    define WEB_ATTRIBUTE_MAYBE_UNUSED
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

#define WEB_ARENA_COPY_ITEMS(Dest, Src, Count) (memcpy((Dest), (Src), sizeof(1 ? *(Dest) : *(Src)) * (Count)))

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
            sz NewCapacity = WEB_MAX((Array)->Capacity + 1, (Array)->Count) * 2;               \
            (Array)->Items = WebArenaRealloc((Arena), (Array)->Items, sizeof(*(Array)->Items) * (Array)->Capacity, sizeof(*(Array)->Items) * NewCapacity, alignof(*(Array)->Items)); \
            (Array)->Capacity = NewCapacity;                            \
        }                                                               \
                                                                        \
        (Array)->Items[(Array)->Count] = Element;                       \
        ++(Array)->Count;                                               \
    } while (0)

#define WEB_ARRAY_POP(Array) do { \
    (Array)->Count = WEB_MIN((Array)->Count - 1, 0); \
} while (0)

// TODO(oleh): Since we know the number of elements in the Rhs array, we can first reserve an
// exact amount of memory needed for insertion of all of them.
#define WEB_ARRAY_EXTEND(Arena, Lhs, Rhs) do { \
    for (sz I = 0; I < (Rhs)->Count; ++I) { \
    WEB_ARRAY_PUSH((Arena), (Lhs), (Rhs)->Items[I]); \
    } \
    } while (0)

u64 WebHashFnv1(web_string_view Input);

b32 WebParseS64(web_string_view Input, s64 *Out);

#define WEB_ARRAY_TYPE(T) struct { \
    T *Items; \
    sz Capacity; \
    sz Count; \
}

typedef u8 web_map_meta_flags;

enum : web_map_meta_flags {
    WEB_MAP_META_OCCUPIED = 1 << 0,
};

typedef struct {
    web_map_meta_flags Flags;
} web_map_meta;

#define WEB_MAP_TYPE(KeyType, ValueType) struct { \
    void *HashProc; \
    void *EqProc; \
    KeyType *Keys; \
    ValueType *Values; \
    web_map_meta *Meta; \
    sz Capacity; \
    sz Count; \
}

#define WEB_MAP_DEFAULT_CAP 11

#define WEB_MAP_INIT_CAP(_Arena, _Map, _HashProc, _EqProc, _Cap) do { \
    typedef typeof(*(_Map)->Keys) _key_type; \
    typedef typeof(*(_Map)->Values) _value_type; \
    u64 (*_HashP)(_key_type) = (_HashProc); \
    b32 (*_EqP)(_key_type, _key_type) = (_EqProc); \
    sz _InitCapacity = (_Cap); \
    _InitCapacity = _InitCapacity > 0 ? _InitCapacity : WEB_MAP_DEFAULT_CAP; \
    (_Map)->Capacity = _InitCapacity; \
    (_Map)->Count = 0; \
    (_Map)->Keys = WEB_ARENA_NEW_MANY(_Arena, _key_type, (_Map)->Capacity); \
    (_Map)->Values = WEB_ARENA_NEW_MANY(_Arena, _value_type, (_Map)->Capacity); \
    (_Map)->Meta = WEB_ARENA_NEW_MANY(_Arena, web_map_meta, (_Map)->Capacity); \
    (_Map)->HashProc = _HashP; \
    (_Map)->EqProc = _EqP; \
} while (0)

#define WEB_MAP_CAPACITY_STEP(_Cap) ((_Cap + 1) * 3)

#define WEB_MAP_FOREACH(_Map, _IdxName) for (sz _IdxName = 0; _IdxName < (_Map)->Capacity; ++_IdxName) if ((_Map)->Meta[_IdxName].Flags & WEB_MAP_META_OCCUPIED)

#define _WEB_MAP_DO_INSERT(_Map, _Key, _Value) do { \
    typedef typeof(*(_Map)->Keys) _key_type; \
    typedef typeof(*(_Map)->Values) _value_type; \
    _key_type _KeyToInsert = (_Key); \
    _value_type _ValueToInsert = (_Value); \
    u64 (*_HashProc)(_key_type) = (_Map)->HashProc; \
    b32 (*_EqProc)(_key_type, _key_type) = (_Map)->EqProc; \
    u64 _Hash = _HashProc(_KeyToInsert); \
    sz _KeyIdx = (sz)(_Hash % (u64)(_Map)->Capacity); \
    while (1) { \
        web_map_meta *_Meta = &(_Map)->Meta[_KeyIdx]; \
        if (!(_Meta->Flags & WEB_MAP_META_OCCUPIED)) { \
            (_Map)->Values[_KeyIdx] = _ValueToInsert; \
            (_Map)->Keys[_KeyIdx] = _KeyToInsert; \
            _Meta->Flags |= WEB_MAP_META_OCCUPIED; \
            (_Map)->Count += 1; \
            break; \
        } \
        _key_type _OccupiedKey = (_Map)->Keys[_KeyIdx]; \
        if (_EqProc(_KeyToInsert, _OccupiedKey)) { \
            (_Map)->Values[_KeyIdx] = _ValueToInsert; \
            break; \
        } \
        _KeyIdx += 1; \
        if (_KeyIdx >= (_Map)->Capacity) _KeyIdx = 0; \
    } \
} while (0)

#define WEB_MAP_GROW(_Arena, _Map, _Cap) do { \
    sz _DesiredCapacity = (_Cap); \
    if (_DesiredCapacity <= (_Map)->Capacity) break; \
    typedef typeof(*(_Map)->Keys) _key_type; \
    typeof(*(_Map)) _NewMap = {0}; \
    WEB_MAP_INIT_CAP(_Arena, &_NewMap, (u64 (*)(_key_type))(_Map)->HashProc, (b32 (*)(_key_type, _key_type))(_Map)->EqProc, _DesiredCapacity); \
    WEB_MAP_FOREACH((_Map), _KeyIdx) { \
        _WEB_MAP_DO_INSERT(&_NewMap, (_Map)->Keys[_KeyIdx], (_Map)->Values[_KeyIdx]); \
    } \
    *(_Map) = _NewMap; \
} while (0)

#define WEB_MAP_LOAD_CEILING 70

#define WEB_MAP_INSERT(_Arena, _Map, _Key, _Value) do { \
    sz _MapLoad = (_Map)->Count * 100 / (_Map)->Capacity; \
    if (_MapLoad >= WEB_MAP_LOAD_CEILING) { \
        sz _NewCapacity = WEB_MAP_CAPACITY_STEP((_Map)->Capacity); \
        WEB_MAP_GROW(_Arena, _Map, _NewCapacity); \
    } \
    _WEB_MAP_DO_INSERT(_Map, _Key, _Value); \
} while (0)

#define WEB_MAP_GET(_Map, _Key, _Out) ({ \
    typedef typeof(*(_Map)->Keys) _key_type; \
    typedef typeof(*(_Map)->Values) _value_type; \
    u64 (*_HashP)(_key_type) = (_Map)->HashProc; \
    b32 (*_EqP)(_key_type, _key_type) = (_Map)->EqProc; \
    _value_type *_OutValue = (_Out); \
    _key_type _GetKey = (_Key); \
    u64 _Hash = _HashP(_GetKey); \
    sz _KeyIdx = (sz)(_Hash % (u64)(_Map)->Capacity); \
    sz _StartingIdx = _KeyIdx; \
    b32 _Result = 0; \
    do { \
        web_map_meta _Meta = (_Map)->Meta[_KeyIdx]; \
        if (_Meta.Flags & WEB_MAP_META_OCCUPIED) { \
            _key_type _OccupiedKey = (_Map)->Keys[_KeyIdx]; \
            if (_EqP(_GetKey, _OccupiedKey)) { \
                if (_OutValue != NULL) *_OutValue = (_Map)->Values[_KeyIdx]; \
                _Result = 1; \
                break; \
            } \
        } \
        _KeyIdx += 1; \
        if (_KeyIdx >= (_Map)->Capacity) _KeyIdx = 0; \
    } while (_KeyIdx != _StartingIdx); \
    _Result; \
})

#define WEB_MAP_CONTAINS(Map, Key) WEB_MAP_GET(Map, Key, NULL)

#define WEB_MAP_INIT(Arena, Hash, Eq, Map) WEB_MAP_INIT_CAP(Arena, Map, Hash, Eq, WEB_MAP_DEFAULT_CAP)

#define WEB_STRING_MAP_INIT_CAP(Arena, Map, Cap) WEB_MAP_INIT_CAP(Arena, Map, WebHashFnv1, WebStringViewEqual, Cap)
#define WEB_STRING_MAP_INIT(Arena, Map) WEB_STRING_MAP_INIT_CAP(Arena, Map, WEB_MAP_DEFAULT_CAP)

#define WEB_MAP_RESET(_Map) do { \
    WEB_MAP_FOREACH((_Map), _KeyIdx) { \
        web_map_meta *_Meta = &(_Map)->Meta[_KeyIdx]; \
        WEB_STRUCT_ZERO(_Meta); \
    } \
    (_Map)->Count = 0; \
} while (0)

#define WEB_MAP_COPY(_Arena, _Dest, _Src) do { \
    typeof((_Src)) _SrcMap = (_Src); \
    typeof(_SrcMap) _DestMap = (_Dest); \
    memcpy(_DestMap, _SrcMap, sizeof(*(_DestMap))); \
    _DestMap->Keys = WEB_ARENA_NEW_MANY(_Arena, typeof(*_SrcMap->Keys), _SrcMap->Capacity); \
    WEB_ARENA_COPY_ITEMS(_DestMap->Keys, _SrcMap->Keys, _SrcMap->Capacity); \
    _DestMap->Values = WEB_ARENA_NEW_MANY(_Arena, typeof(*_SrcMap->Values), _SrcMap->Capacity); \
    WEB_ARENA_COPY_ITEMS(_DestMap->Values, _SrcMap->Values, _SrcMap->Capacity); \
    _DestMap->Meta = WEB_ARENA_NEW_MANY(_Arena, typeof(*_SrcMap->Meta), _SrcMap->Capacity); \
    WEB_ARENA_COPY_ITEMS(_DestMap->Meta, _SrcMap->Meta, _SrcMap->Capacity); \
} while (0)

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // WEB_COMMON_H_
