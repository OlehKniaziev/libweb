#include "log.h"

#include <stdarg.h>

static FILE *LogDestination;
static web_log_level LogLevel;
static b32 LogSource = 1;

static const char *LevelStrTable[N_WEB_LOG_LEVEL] = {
    #define X(Level) [WEB_LOG_LEVEL_##Level] = #Level,
    WEB_ENUM_LOG_LEVELS
    #undef X
};

static const char *ScopeStrTable[N_WEB_LOG_SCOPE] = {
    #define X(Scope) [WEB_LOG_SCOPE_##Scope] = #Scope,
    WEB_ENUM_LOG_SCOPES
    #undef X
};

void WebLog(web_log_level Level,
            web_log_scope Scope,
            web_log_source_info Source,
            const char *Fmt,
            ...) {
    if (Level < LogLevel) return;

    va_list Args = {0};

    va_start(Args, Fmt);
    sz MessageCount = vsnprintf(NULL, 0, Fmt, Args);
    va_end(Args);

    web_arena *Temp = WebGetTempArena();

    char *Message = WebArenaPush(Temp, MessageCount + 1);

    va_start(Args, Fmt);
    vsnprintf(Message, MessageCount + 1, Fmt, Args);
    va_end(Args);

    const char *LevelStr = LevelStrTable[Level];
    const char *ScopeStr = ScopeStrTable[Scope];

    if (LogSource) {
        fprintf(LogDestination,
                "[%s][%s] <%s:%d(%s)> %s",
                LevelStr,
                ScopeStr,
                Source.FileName,
                Source.Line,
                Source.ProcName,
                Message);
    } else {
        fprintf(LogDestination, "[%s][%s] %s", LevelStr, ScopeStr, Message);
    }
}

void WebLogSetLevel(web_log_level Level) {
    LogLevel = Level;
}

void WebLogSetDestination(FILE *Dest) {
    LogDestination = Dest;
}

void WebLogSetIncludeSource(b32 Value) {
    LogSource = Value;
}
