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
    return JsonIsWhitespace(Char) ||
           Char == '{' ||
           Char == '}' ||
           Char == '[' ||
           Char == ']' ||
           Char == '"' ||
           Char == ',' ||
           Char == ':';
}

static b32 JsonNextToken(web_arena *Arena, web_string_view Input, sz *Position, json_token *OutToken) {
    sz CurrentPosition = *Position;

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
        ++CurrentPosition;
        struct {
            u8 *Items;
            uz Capacity;
            uz Count;
        } String;
        WEB_ARRAY_INIT(Arena, &String);

        for (; CurrentPosition < Input.Count; ++CurrentPosition) {
            u8 Char = Input.Items[CurrentPosition];
            if (Char == '"') break;

            if (Char == '\\') {
                ++CurrentPosition;
                if (CurrentPosition >= Input.Count) {
                    break;
                }

                Char = Input.Items[CurrentPosition];

                switch (Char) {
                case 'n': {
                    WEB_ARRAY_PUSH(Arena, &String, '\n');
                    break;
                }
                case 'r': {
                    WEB_ARRAY_PUSH(Arena, &String, '\r');
                    break;
                }
                case '"': {
                    WEB_ARRAY_PUSH(Arena, &String, '"');
                    break;
                }
                case '\\': {
                    WEB_ARRAY_PUSH(Arena, &String, '\\');
                    break;
                }
                default: WEB_TODO();
                }
            } else {
                WEB_ARRAY_PUSH(Arena, &String, Char);
            }
        }

        if (CurrentPosition >= Input.Count) {
            OutToken->Type = TOKEN_UNCLOSED_STRING;
        } else {
            OutToken->Type = TOKEN_STRING;
        }

        OutToken->Value.Items = String.Items;
        OutToken->Value.Count = String.Count;

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

            for (sz I = 0; I < Value.Count; ++I) {
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

static b32 JsonPeekToken(web_string_view Input, sz *Position, json_token *OutToken) {
    web_arena *Temp = WebGetTempArena();

    sz SavedPosition = *Position;
    b32 Result = JsonNextToken(Temp, Input, Position, OutToken);
    *Position = SavedPosition;
    return Result;
}

static f64 ParseF64(web_string_view Buffer) {
    WEB_ASSERT(Buffer.Count != 0);

    u64 Mult = 1;
    u64 Result = 0;
    for (sz I = Buffer.Count - 1; I >= 0; --I) {
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

static b32 JsonParseValue(web_arena *Arena, web_string_view Input, sz *Position, web_json_value *OutValue) {
    json_token Token;

    if (!JsonNextToken(Arena, Input, Position, &Token)) return 0;

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
                WEB_ASSERT(JsonNextToken(Arena, Input, Position, &Token));
                break;
            }

        ParseElement: ;
            web_json_value Element;
            if (!JsonParseValue(Arena, Input, Position, &Element)) return 0;

            WEB_ARRAY_PUSH(Arena, &Elements, Element);

            if (!JsonNextToken(Arena, Input, Position, &Token)) return 0;
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
                WEB_ASSERT(JsonNextToken(Arena, Input, Position, &Token));
                break;
            }

        ParseKeyValue:
            if (!JsonNextToken(Arena, Input, Position, &Token)) return 0;
            if (Token.Type != TOKEN_STRING) return 0;

            web_string_view KeyToInsert = Token.Value;

            if (!JsonNextToken(Arena, Input, Position, &Token)) return 0;
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

            sz ObjectIndex = (sz) WebHashFnv1(KeyToInsert) % Object.Capacity;

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

            if (!JsonNextToken(Arena, Input, Position, &Token)) return 0;
            if (Token.Type == TOKEN_RBRACE) break;
            if (Token.Type == TOKEN_COMMA) goto ParseKeyValue;
        }

        OutValue->Type = JSON_OBJECT;
        OutValue->Object = Object;
        return 1;
    }
    default: {
        return 0;
    }
    }
}

b32 WebJsonParse(web_arena *Arena, web_string_view Input, web_json_value *OutValue) {
    sz Position = 0;
    return JsonParseValue(Arena, Input, &Position, OutValue);
}

b32 WebJsonObjectGet(const web_json_object *Object, web_string_view SearchKey, web_json_value *OutValue) {
    sz StartIndex = (sz) WebHashFnv1(SearchKey) % Object->Capacity;
    sz CurrentIndex = StartIndex;

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

b32 WebJsonObjectGetU32(const web_json_object *Object, web_string_view Key, u32 *OutValue) {
    f64 OutF64 = 0.0;
    if (!WebJsonObjectGetNumber(Object, Key, &OutF64)) {
        return 0;
    }

    f64 Integral;
    f64 Fractional = modf(OutF64, &Integral);
    if (Fractional != 0.0) {
        return 0;
    }

    if (Integral < 0.0 || Integral > (f64) UINT32_MAX) {
        return 0;
    }

    *OutValue = (u32) Integral;
    return 1;
}

b32 WebJsonObjectGetU64(const web_json_object *Object, web_string_view Key, u64 *OutValue) {
    f64 OutF64 = 0.0;
    if (!WebJsonObjectGetNumber(Object, Key, &OutF64)) {
        return 0;
    }

    f64 Integral;
    f64 Fractional = modf(OutF64, &Integral);
    if (Fractional != 0.0) {
        return 0;
    }

    if (Integral < 0.0 || Integral > (f64) UINT64_MAX) {
        return 0;
    }

    *OutValue = (u64) Integral;
    return 1;
}

typedef enum {
    STATE_CLEAN,
    STATE_DIRTY,
} json_state;

typedef struct {
    web_arena *Arena;
    json_state State;
    web_dynamic_string OutputString;
} writer_state;

static void WriteChar(writer_state *Writer, u8 Char) {
    WEB_ARRAY_PUSH(Writer->Arena, &Writer->OutputString, Char);
}

web_json_writer WebJsonBegin(web_arena *Arena) {
    writer_state *Writer = WEB_ARENA_NEW(Arena, writer_state);
    Writer->Arena = Arena;
    Writer->State = STATE_CLEAN;
    WEB_ARRAY_INIT(Writer->Arena, &Writer->OutputString);
    return (web_json_writer) Writer;
}

web_string_view WebJsonEnd(web_json_writer WriterPtr) {
    writer_state *Writer = (writer_state *) WriterPtr;
    web_string_view Result = {.Items = Writer->OutputString.Items, .Count = Writer->OutputString.Count};
    return Result;
}

void WebJsonBeginObject(web_json_writer WriterPtr) {
    writer_state *Writer = (writer_state *) WriterPtr;
    WriteChar(Writer, '{');
    Writer->State = STATE_CLEAN;
}

void WebJsonEndObject(web_json_writer WriterPtr) {
    writer_state *Writer = (writer_state *) WriterPtr;
    WriteChar(Writer, '}');
    Writer->State = STATE_DIRTY;
}

void WebJsonBeginArray(web_json_writer WriterPtr) {
    writer_state *Writer = (writer_state *) WriterPtr;
    WriteChar(Writer, '[');
    Writer->State = STATE_CLEAN;
}

void WebJsonEndArray(web_json_writer WriterPtr) {
    writer_state *Writer = (writer_state *) WriterPtr;
    WriteChar(Writer, ']');
    Writer->State = STATE_DIRTY;
}

static void WriteStringLiteral(writer_state *Writer, web_string_view String) {
    WriteChar(Writer, '"');

    for (sz StringIndex = 0; StringIndex < String.Count; ++StringIndex) {
        u8 Char = String.Items[StringIndex];

        if (Char == '"') {
            WriteChar(Writer, '\\');
        }

        WriteChar(Writer, Char);
    }

    WriteChar(Writer, '"');
}


void WebJsonPutKey(web_json_writer WriterPtr, web_string_view Key) {
    writer_state *Writer = (writer_state *) WriterPtr;

    if (Writer->State == STATE_DIRTY) {
        WriteChar(Writer, ',');
    }

    WriteStringLiteral(Writer, Key);

    WriteChar(Writer, ':');
}

static void WriteSpecial(writer_state *Writer, const char *Special) {
    WEB_ARRAY_EXTEND(Writer->Arena, &Writer->OutputString, &WEB_SV_LIT(Special));
    Writer->State = STATE_DIRTY;
}

void WebJsonPutTrue(web_json_writer WriterPtr) {
    WriteSpecial((writer_state *) WriterPtr, "true");
}

void WebJsonPutFalse(web_json_writer WriterPtr) {
    WriteSpecial((writer_state *) WriterPtr, "false");
}

void WebJsonPutNull(web_json_writer WriterPtr) {
    WriteSpecial((writer_state *) WriterPtr, "null");
}

void WebJsonPutNumber(web_json_writer WriterPtr, f64 Number) {
    writer_state *Writer = (writer_state *) WriterPtr;
    web_arena *TempArena = WebGetTempArena();

    web_string_view NumberString;
    f64 Integral;
    f64 Fractional = modf(Number, &Integral);
    if (fabs(Fractional) == 0.0) {
        NumberString = WebArenaFormat(TempArena, "%lld", (s64)Number);
    } else {
        NumberString = WebArenaFormat(TempArena, "%f", Number);
    }

    WEB_ARRAY_EXTEND(Writer->Arena, &Writer->OutputString, &NumberString);
}

void WebJsonPutString(web_json_writer WriterPtr, web_string_view String) {
    writer_state *Writer = (writer_state *) WriterPtr;
    WriteStringLiteral(Writer, String);
}

void WebJsonPrepareArrayElement(web_json_writer WriterPtr) {
    writer_state *Writer = (writer_state *) WriterPtr;
    if (Writer->State == STATE_DIRTY) {
        WriteChar(Writer, ',');
    }
}
