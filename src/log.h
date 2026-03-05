#ifndef LOG_H_
#define LOG_H_

#include "common.h"

#include <stdio.h>

#define WEB_ENUM_LOG_LEVELS \
    X(INFO) \
    X(WARN) \
    X(ERROR) \
    X(NONE)

typedef enum {
    #define X(Level) WEB_LOG_LEVEL_##Level,
    WEB_ENUM_LOG_LEVELS
    #undef X
    N_WEB_LOG_LEVEL,
} web_log_level;

#define WEB_ENUM_LOG_SCOPES \
    X(NONE) \
    X(HTTP) \
    X(TLS) \
    X(COMMON) \
    X(JSON) \
    X(BASE64)

typedef enum {
    #define X(Scope) WEB_LOG_SCOPE_##Scope,
    WEB_ENUM_LOG_SCOPES
    #undef X
    N_WEB_LOG_SCOPE,
} web_log_scope;

typedef struct {
    const char *FileName;
    const char *ProcName;
    u32 Line;
} web_log_source_info;

#define WEB_LOG_FMT(Level, Scope, Fmt, ...) (WebLog(WEB_LOG_LEVEL_##Level, WEB_LOG_SCOPE_##Scope, (web_log_source_info) {.FileName = __FILE__, .ProcName = __FUNCTION__, .Line = __LINE__}, (Fmt), __VA_ARGS__))
#define WEB_LOG(Level, Scope, Msg) WEB_LOG_FMT(Level, Scope, "%s", (Msg))

#define WEB_LOG_FATAL_FMT(Fmt, ...) do { \
    WEB_LOG_FMT(ERROR, NONE, (Fmt), __VA_ARGS__); \
    exit(1); \
} while (0)
#define WEB_LOG_FATAL(Msg) WEB_LOG_FATAL_FMT("%s", (Msg))

void WebLog(web_log_level Level,
            web_log_scope Scope,
            web_log_source_info Source,
            const char *Fmt,
            ...);

void WebLogSetDestination  (FILE *);
void WebLogSetLevel        (web_log_level);
void WebLogSetIncludeSource(b32);

#endif // LOG_H_
