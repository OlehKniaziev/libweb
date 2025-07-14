#include <math.h>

#include "json.h"

typedef struct {
    enum {
        TOKEN_LBRACKET,
        TOKEN_RBRACKET,
        TOKEN_LBRACE,
        TOKEN_RBRACE,
        TOKEN_STRING,
        TOKEN_NUMBER,
        TOKEN_NULL,
        TOKEN_TRUE,
        TOKEN_FALSE,
        TOKEN_COLON,
        TOKEN_COMMA,

        TOKEN_ILLEGAL,
        TOKEN_UNCLOSED_STRING,
    } Type;
    web_string_view Value;
} json_token;

static inline b32 JsonIsWhitespace(u8 Char) {
    return Char == 0x20 || Char == 0x0A || Char == 0x0D || Char == 0x09;
}

static inline b32 JsonIsTerminalOrWhitespace(u8 Char) {
    return JsonIsWhitespace(Char) || Char == '{' || Char == '}' || Char == '[' || Char == ']' || Char == '"';
}

static b32 JsonNextToken(web_arena *Arena, web_string_view Input, uz *Position, json_token *OutToken) {
    uz CurrentPosition = *Position;

    for (; CurrentPosition < Input.Count; ++CurrentPosition) {
        if (!JsonIsWhitespace(Input.Items[CurrentPosition])) break;
    }

    if (CurrentPosition >= Input.Count) return 0;

    u8 CurrentChar = Input.Items[CurrentPosition];

    switch (CurrentChar) {
    case '[': {
        OutToken->Type = TOKEN_LBRACKET;
        OutToken->Value.Items = Input.Items + CurrentPosition;
        OutToken->Value.Count = 1;
        *Position += 1;
        return 1;
    }
    case ']': {
        OutToken->Type = TOKEN_RBRACKET;
        OutToken->Value.Items = Input.Items + CurrentPosition;
        OutToken->Value.Count = 1;
        *Position += 1;
        return 1;
    }
    case '{': {
        OutToken->Type = TOKEN_LBRACE;
        OutToken->Value.Items = Input.Items + CurrentPosition;
        OutToken->Value.Count = 1;
        *Position += 1;
        return 1;
    }
    case '}': {
        OutToken->Type = TOKEN_RBRACE;
        OutToken->Value.Items = Input.Items + CurrentPosition;
        OutToken->Value.Count = 1;
        *Position += 1;
        return 1;
    }
    case ',': {
        OutToken->Type = TOKEN_COMMA;
        OutToken->Value.Items = Input.Items + CurrentPosition;
        OutToken->Value.Count = 1;
        *Position += 1;
        return 1;
    }
    case ':': {
        OutToken->Type = TOKEN_COLON;
        OutToken->Value.Items = Input.Items + CurrentPosition;
        OutToken->Value.Count = 1;
        *Position += 1;
        return 1;
    }
    case '"': {
        uz StringStart = CurrentPosition + 1;
        for (CurrentPosition = StringStart; CurrentPosition < Input.Count; ++CurrentPosition) {
            if (Input.Items[CurrentPosition] == '"') break;
        }

        if (CurrentPosition >= Input.Count) {
            OutToken->Type = TOKEN_UNCLOSED_STRING;
            --StringStart;
        } else {
            OutToken->Type = TOKEN_STRING;
        }

        OutToken->Value.Items = Input.Items + StringStart;
        OutToken->Value.Count = CurrentPosition - StringStart;

        *Position = CurrentPosition + 1;
        return 1;
    }
    default: {
        uz ValueStart = CurrentPosition;
        for (; CurrentPosition < Input.Count; ++CurrentPosition) {
            u8 Char = Input.Items[CurrentPosition];
            if (JsonIsTerminalOrWhitespace(Char)) break;
        }

        web_string_view Value = {.Items = Input.Items + ValueStart, .Count = CurrentPosition - ValueStart};
        if (WebStringViewEqualCStr(Value, "true")) {
            OutToken->Type = TOKEN_TRUE;
        } else if (WebStringViewEqualCStr(Value, "false")) {
            OutToken->Type = TOKEN_FALSE;
        } else if (WebStringViewEqualCStr(Value, "null")) {
            OutToken->Type = TOKEN_NULL;
        } else {
            int TokenType = TOKEN_NUMBER;

            for (uz I = 0; I < Value.Count; ++I) {
                u8 Char = Value.Items[I];
                if (Char < '0' || Char > '9') {
                    TokenType = TOKEN_ILLEGAL;
                    break;
                }
            }

            OutToken->Type = TokenType;
        }

        *Position = CurrentPosition;
        OutToken->Value = Value;
        return 1;
    }
    }
}

static b32 JsonPeekToken(web_string_view Input, uz *Position, json_token *OutToken) {
    uz SavedPosition = *Position;
    b32 Result = JsonNextToken(Input, Position, OutToken);
    *Position = SavedPosition;
    return Result;
}

static f64 ParseF64(web_string_view Buffer) {
    WEB_ASSERT(Buffer.Count != 0);

    u64 Mult = 1;
    u64 Result = 0;
    for (uz I = Buffer.Count - 1; I > 0; --I) {
        u8 Char = Buffer.Items[I];
        // TODO(oleh): Provide a way to signal error to the caller.
        if (Char < '0' || Char > '9') WEB_PANIC("Bad input to 'ParseF64'");
        Result += Mult * (Char - '0');
        Mult *= 10;
    }

    if (Buffer.Items[0] == '-') {
        return (f64)-(s64)Result;
    } else if (Buffer.Items[0] == '+') {
        return (f64)Result;
    } else if (Buffer.Count == 1) {
        u8 Char = Buffer.Items[0];
        WEB_ASSERT(Char >= '0' && Char <= '9');
        return (f64)(Char - '0');
    }

    return (f64)Result;
}

#define DEFAULT_OBJECT_CAPACITY 37

static b32 JsonParseValue(web_arena *Arena, web_string_view Input, uz *Position, web_json_value *OutValue) {
    json_token Token;

    if (!JsonNextToken(Input, Position, &Token)) return 0;

    switch (Token.Type) {
    case TOKEN_NUMBER: {
        f64 NumberValue = ParseF64(Token.Value);
        OutValue->Type = JSON_NUMBER;
        OutValue->Number = NumberValue;
        return 1;
    }
    case TOKEN_STRING: {
        OutValue->Type = JSON_STRING;
        OutValue->String = Token.Value;
        return 1;
    }
    case TOKEN_TRUE: {
        OutValue->Type = JSON_TRUE;
        return 1;
    }
    case TOKEN_FALSE: {
        OutValue->Type = JSON_FALSE;
        return 1;
    }
    case TOKEN_NULL: {
        OutValue->Type = JSON_NULL;
        return 1;
    }
    case TOKEN_LBRACKET: {
        web_json_array Elements;
        WEB_ARRAY_INIT(Arena, &Elements);

        while (1) {
            if (!JsonPeekToken(Input, Position, &Token)) return 0;
            if (Token.Type == TOKEN_RBRACKET) {
                WEB_ASSERT(JsonNextToken(Input, Position, &Token));
                break;
            }

        ParseElement: ;
            web_json_value Element;
            if (!JsonParseValue(Arena, Input, Position, &Element)) return 0;

            WEB_ARRAY_PUSH(Arena, &Elements, Element);

            if (!JsonNextToken(Input, Position, &Token)) return 0;
            if (Token.Type == TOKEN_RBRACKET) break;
            if (Token.Type == TOKEN_COMMA) goto ParseElement;
        }

        OutValue->Type = JSON_ARRAY;
        OutValue->Array = Elements;
        return 1;
    }
    case TOKEN_LBRACE: {
        web_json_object Object;
        Object.Capacity = DEFAULT_OBJECT_CAPACITY;
        Object.Keys = WEB_ARENA_PUSH_ZERO(Arena, sizeof(*Object.Keys) * DEFAULT_OBJECT_CAPACITY);
        Object.Values = WEB_ARENA_PUSH_ZERO(Arena, sizeof(*Object.Values) * DEFAULT_OBJECT_CAPACITY);
        Object.Count = 0;

        while (1) {
            if (!JsonPeekToken(Input, Position, &Token)) return 0;
            if (Token.Type == TOKEN_RBRACE) {
                WEB_ASSERT(JsonNextToken(Input, Position, &Token));
                break;
            }

        ParseKeyValue:
            if (!JsonNextToken(Input, Position, &Token)) return 0;
            if (Token.Type != TOKEN_STRING) return 0;

            web_string_view KeyToInsert = Token.Value;

            if (!JsonNextToken(Input, Position, &Token)) return 0;
            if (Token.Type != TOKEN_COLON) return 0;

            web_json_value ValueToInsert;

            if (!JsonParseValue(Arena, Input, Position, &ValueToInsert)) return 0;

            uz ObjectLoadPercentage = 100 * Object.Count / Object.Capacity;

            if (ObjectLoadPercentage >= 65) {
                uz NewCapacity = (Object.Capacity + 1) * 3;
                Object.Keys = WebArenaRealloc(Arena, Object.Keys, Object.Capacity * sizeof(*Object.Keys), NewCapacity * sizeof(*Object.Keys));
                Object.Values = WebArenaRealloc(Arena, Object.Values, Object.Capacity * sizeof(*Object.Values), NewCapacity * sizeof(*Object.Values));
                Object.Capacity = NewCapacity;
            }

            u64 ObjectIndex = WebHashFnv1(KeyToInsert) % Object.Capacity;

            while (1) {
                web_string_view CurrentKey = Object.Keys[ObjectIndex];
                if (CurrentKey.Items == NULL) {
                    Object.Keys[ObjectIndex] = KeyToInsert;
                    Object.Values[ObjectIndex] = ValueToInsert;
                    ++Object.Count;
                    break;
                }

                if (WebStringViewEqual(CurrentKey, KeyToInsert)) {
                    WEB_PANIC_FMT("Tried to insert a duplicate key '" WEB_SV_FMT "' into an object", WEB_SV_ARG(KeyToInsert));
                }

                ++ObjectIndex;
                if (ObjectIndex >= Object.Capacity) ObjectIndex = 0;
            }

            if (!JsonNextToken(Input, Position, &Token)) return 0;
            if (Token.Type == TOKEN_RBRACE) break;
            if (Token.Type == TOKEN_COMMA) goto ParseKeyValue;
        }

        OutValue->Type = JSON_OBJECT;
        OutValue->Object = Object;
        return 1;
    }
    default: WEB_TODO();
    }
}

b32 WebJsonParse(web_arena *Arena, web_string_view Input, web_json_value *OutValue) {
    uz Position = 0;
    return JsonParseValue(Arena, Input, &Position, OutValue);
}

b32 WebJsonObjectGet(const web_json_object *Object, web_string_view SearchKey, web_json_value *OutValue) {
    u64 StartIndex = WebHashFnv1(SearchKey) % Object->Capacity;
    u64 CurrentIndex = StartIndex;

    do {
        web_string_view CurrentKey = Object->Keys[CurrentIndex];
        if (CurrentKey.Items != NULL && WebStringViewEqual(CurrentKey, SearchKey)) {
            *OutValue = Object->Values[CurrentIndex];
            return 1;
        }

        ++CurrentIndex;

        if (CurrentIndex >= Object->Capacity) CurrentIndex = 0;
    } while (CurrentIndex != StartIndex);

    return 0;
}

b32 JsonObjectGet_string_view(const web_json_object *Object, web_string_view Key, web_string_view *OutValue) {
    web_json_value JsonValue;
    if (!WebJsonObjectGet(Object, Key, &JsonValue)) return 0;
    if (JsonValue.Type != JSON_STRING) return 0;
    *OutValue = JsonValue.String;
    return 1;
}

static web_arena *CurrentJsonArena;
static uz CurrentJsonStart;

enum {
    STATE_CLEAN,
    STATE_DIRTY,
} CurrentJsonState;

void WebJsonBegin(web_arena *Arena) {
    CurrentJsonArena = Arena;
    CurrentJsonStart = Arena->Offset;
    CurrentJsonState = STATE_CLEAN;
}

web_string_view WebJsonEnd(void) {
    uz JsonCount = CurrentJsonArena->Offset - CurrentJsonStart;
    web_string_view Result = {.Items = CurrentJsonArena->Items + CurrentJsonStart, .Count = JsonCount};
    CurrentJsonArena->Offset = WebAlignForward(CurrentJsonArena->Offset, sizeof(uz));
    return Result;
}

void WebJsonBeginObject(void) {
    WEB_ASSERT(CurrentJsonArena->Capacity - CurrentJsonArena->Offset >= 1);
    u8 *Ptr = CurrentJsonArena->Items + CurrentJsonArena->Offset;
    *Ptr = '{';
    CurrentJsonArena->Offset += 1;
    CurrentJsonState = STATE_CLEAN;
}

void WebJsonEndObject(void) {
    WEB_ASSERT(CurrentJsonArena->Capacity - CurrentJsonArena->Offset >= 1);
    u8 *Ptr = CurrentJsonArena->Items + CurrentJsonArena->Offset;
    *Ptr = '}';
    CurrentJsonArena->Offset += 1;
    CurrentJsonState = STATE_DIRTY;
}

void WebJsonBeginArray(void) {
    WEB_ASSERT(CurrentJsonArena->Capacity - CurrentJsonArena->Offset >= 1);
    u8 *Ptr = CurrentJsonArena->Items + CurrentJsonArena->Offset;
    *Ptr = '[';
    CurrentJsonArena->Offset += 1;
    CurrentJsonState = STATE_CLEAN;
}

void WebJsonEndArray(void) {
    WEB_ASSERT(CurrentJsonArena->Capacity - CurrentJsonArena->Offset >= 1);
    u8 *Ptr = CurrentJsonArena->Items + CurrentJsonArena->Offset;
    *Ptr = ']';
    CurrentJsonArena->Offset += 1;
    CurrentJsonState = STATE_DIRTY;
}

void WebJsonPutKey(web_string_view Key) {
    uz BytesRequired;

    if (CurrentJsonState == STATE_CLEAN) {
        BytesRequired = Key.Count + 3;

        WEB_ASSERT(CurrentJsonArena->Capacity - CurrentJsonArena->Offset >= BytesRequired);

        u8 *Ptr = CurrentJsonArena->Items + CurrentJsonArena->Offset;
        *Ptr = '"';
        memcpy(Ptr + 1, Key.Items, Key.Count);
        Ptr[Key.Count + 1] = '"';
        Ptr[Key.Count + 2] = ':';
    } else {
        BytesRequired = Key.Count + 4;

        WEB_ASSERT(CurrentJsonArena->Capacity - CurrentJsonArena->Offset >= BytesRequired);

        u8 *Ptr = CurrentJsonArena->Items + CurrentJsonArena->Offset;
        *Ptr = ',';
        Ptr[1] = '"';
        memcpy(Ptr + 2, Key.Items, Key.Count);
        Ptr[Key.Count + 2] = '"';
        Ptr[Key.Count + 3] = ':';
    }

    CurrentJsonArena->Offset += BytesRequired;
}

static void WebJsonPutSpecial(web_string_view Special) {
    uz BytesRequired = Special.Count;
    WEB_ASSERT(CurrentJsonArena->Capacity - CurrentJsonArena->Offset >= BytesRequired);

    u8 *Ptr = CurrentJsonArena->Items + CurrentJsonArena->Offset;
    memcpy(Ptr, Special.Items, Special.Count);

    CurrentJsonState = STATE_DIRTY;
    CurrentJsonArena->Offset += BytesRequired;
}

void WebJsonPutTrue(void) {
    WebJsonPutSpecial(WEB_SV_LIT("true"));
}

void WebJsonPutFalse(void) {
    WebJsonPutSpecial(WEB_SV_LIT("false"));
}

void WebJsonPutNull(void) {
    WebJsonPutSpecial(WEB_SV_LIT("null"));
}

void WebJsonPutNumber(f64 Number) {
    web_arena *TempArena = WebGetTempArena();

    web_string_view NumberString;
    f64 Integral;
    f64 Fractional = modf(Number, &Integral);
    if (Fractional == 0.0 || Fractional == -0.0) {
        NumberString = WebArenaFormat(TempArena, "%lld", (s64)Number);
    } else {
        NumberString = WebArenaFormat(TempArena, "%f", Number);
    }

    uz BytesRequired = NumberString.Count;
    WEB_ASSERT(CurrentJsonArena->Capacity - CurrentJsonArena->Offset >= BytesRequired);

    u8 *Ptr = CurrentJsonArena->Items + CurrentJsonArena->Offset;
    memcpy(Ptr, NumberString.Items, NumberString.Count);

    CurrentJsonState = STATE_DIRTY;
    CurrentJsonArena->Offset += BytesRequired;
}

void WebJsonPutString(web_string_view String) {
    uz BytesRequired = String.Count + 2;
    WEB_ASSERT(CurrentJsonArena->Capacity - CurrentJsonArena->Offset >= BytesRequired);

    u8 *Ptr = CurrentJsonArena->Items + CurrentJsonArena->Offset;
    Ptr[0] = '"';
    memcpy(Ptr + 1, String.Items, String.Count);
    Ptr[String.Count + 1] = '"';

    CurrentJsonState = STATE_DIRTY;
    CurrentJsonArena->Offset += BytesRequired;
}

void WebJsonPrepareArrayElement(void) {
    if (CurrentJsonState == STATE_DIRTY) {
        const uz BytesRequired = 1;

        WEB_ASSERT(CurrentJsonArena->Capacity - CurrentJsonArena->Offset >= BytesRequired);

        u8 *Ptr = CurrentJsonArena->Items + CurrentJsonArena->Offset;
        *Ptr = ',';
        CurrentJsonArena->Offset += BytesRequired;
    }
}
