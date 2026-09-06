#ifndef JSON_H_
#define JSON_H_

#include "common.h"

#ifdef __cplusplus
    extern "C" {
#endif

typedef enum {
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT,
    JSON_TRUE,
    JSON_FALSE,
    JSON_NULL,
} web_json_value_type;

struct web_json_value;
typedef struct web_json_value web_json_value;

typedef struct {
    web_json_value *Items;
    sz Count;
    sz Capacity;
} web_json_array;

typedef struct {
    web_string_view *Keys;
    web_json_value *Values;
    sz Count;
    sz Capacity;
} web_json_object;

struct web_json_value {
    web_json_value_type Type;
    union {
        f64 Number;
        web_string_view String;
        web_json_array Array;
        web_json_object Object;
    };
};

b32 WebJsonParse(web_arena *Arena, web_string_view Input, web_json_value *OutValue);

b32 WebJsonObjectGet(const web_json_object *Object, web_string_view Key, web_json_value *OutValue);

static inline b32 WebJsonObjectGetStringView(const web_json_object *Object, web_string_view Key, web_string_view *OutValue) {
    web_json_value OutJsonValue;
    if (!WebJsonObjectGet(Object, Key, &OutJsonValue)) {
        return 0;
    }

    if (OutJsonValue.Type == JSON_STRING) {
        *OutValue = OutJsonValue.String;
        return 1;
    }

    return 0;
}

static inline b32 WebJsonObjectGetNumber(const web_json_object *Object, web_string_view Key, f64 *OutValue) {
    web_json_value OutJsonValue;
    if (!WebJsonObjectGet(Object, Key, &OutJsonValue)) {
        return 0;
    }

    if (OutJsonValue.Type == JSON_NUMBER) {
        *OutValue = OutJsonValue.Number;
        return 1;
    }

    return 0;
}

b32 WebJsonObjectGetU32(const web_json_object *, web_string_view, u32 *);
b32 WebJsonObjectGetU64(const web_json_object *, web_string_view, u64 *);

static inline b32 WebJsonObjectGetBool(const web_json_object *Object, web_string_view Key, b32 *OutValue) {
    web_json_value OutJsonValue;
    if (!WebJsonObjectGet(Object, Key, &OutJsonValue)) {
        return 0;
    }

    if (OutJsonValue.Type == JSON_TRUE) {
        *OutValue = 1;
        return 1;
    } else if (OutJsonValue.Type == JSON_FALSE) {
        *OutValue = 0;
        return 1;
    }

    return 0;
}

static inline b32 WebJsonObjectGetArray(const web_json_object *Object, web_string_view Key, web_json_array *OutValue) {
    web_json_value OutJsonValue;
    if (!WebJsonObjectGet(Object, Key, &OutJsonValue)) {
        return 0;
    }

    if (OutJsonValue.Type == JSON_ARRAY) {
        *OutValue = OutJsonValue.Array;
        return 1;
    }

    return 0;
}

static inline b32 WebJsonObjectGetObject(const web_json_object *Object, web_string_view Key, web_json_object *OutValue) {
    web_json_value OutJsonValue;
    if (!WebJsonObjectGet(Object, Key, &OutJsonValue)) {
        return 0;
    }

    if (OutJsonValue.Type == JSON_OBJECT) {
        *OutValue = OutJsonValue.Object;
        return 1;
    }

    return 0;
}

typedef void* web_json_writer;

web_json_writer WebJsonBegin(web_arena *);

void WebJsonBeginObject(web_json_writer);
void WebJsonEndObject(web_json_writer);

void WebJsonBeginArray(web_json_writer);
void WebJsonEndArray(web_json_writer);

void WebJsonPrepareArrayElement(web_json_writer);

void WebJsonPutNumber(web_json_writer, f64);
void WebJsonPutString(web_json_writer, web_string_view);

void WebJsonPutTrue(web_json_writer);
void WebJsonPutFalse(web_json_writer);
void WebJsonPutNull(web_json_writer);

void WebJsonPutKey(web_json_writer, web_string_view);

web_string_view WebJsonEnd(web_json_writer);

#ifdef __cplusplus
}
#endif

#endif // JSON_H_
