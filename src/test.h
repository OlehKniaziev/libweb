#ifndef WEB_TEST_H_
#define WEB_TEST_H_

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

typedef struct {
    web_arena Arena;
    web_source_info CurrentSource;
    web_string_view CurrentTestCaseName;
    web_string_view FailMessage;
    b32 Failed;
} web_test_runner;

void WebTestInitRuntime(void);

void WebTestRunnerInit(
        web_test_runner *Runner
);

typedef void (*web_test_runner_proc)(web_test_runner *Runner);

void WebTestRunnerExecute(
        web_test_runner *Runner,
        web_string_view TestCaseName,
        web_test_runner_proc Proc
);

b32 WebTestRunnerReport(web_test_runner *Runner);

void WebTestRunnerSetFail(
        web_test_runner *Runner,
        web_source_info SourceInfo,
        const char *Fmt,
        ...
) WEB_ATTRIBUTE_PRINTF(3, 4);

#define WEB_TEST_CASE_IMPL_NAME(Name) WEB_TEST_CASE__##Name##_IMPL
#define WEB_TEST_CASE_RUN_NAME(Name) WEB_TEST_CASE__##Name##_RUN

#define WEB_TEST_HANDLER_SIG(H) b32 H(void)

#define WEB_DEFINE_TEST(Name) \
    static void WEB_TEST_CASE_IMPL_NAME(Name)(web_test_runner *Runner) WEB_ATTRIBUTE_MAYBE_UNUSED; \
    WEB_TEST_HANDLER_SIG(WEB_TEST_CASE_RUN_NAME(Name)) WEB_ATTRIBUTE_MAYBE_UNUSED; \
    b32 WEB_TEST_CASE_RUN_NAME(Name)(void) { \
        web_test_runner Runner = {0}; \
        WebTestRunnerInit(&Runner); \
        WebTestRunnerExecute(&Runner, WEB_SV_LIT(#Name), WEB_TEST_CASE_IMPL_NAME(Name)); \
        return WebTestRunnerReport(&Runner); \
    } \
    static void WEB_TEST_CASE_IMPL_NAME(Name)(web_test_runner *Runner) 

#define _WEB_T_EQUAL_HANDLER_NAME(Type) _Web_T_Equal_##Type

#define _WEB_T_EQUAL_INT_GEN_IMPL(Type, Fmt) static inline b32 _WEB_T_EQUAL_HANDLER_NAME(Type)(web_test_runner *Runner, Type Lhs, const char *LhsRepr, Type Rhs, const char *RhsRepr, web_source_info SourceInfo, b32 ExpectEqual) { \
    b32 Cond = ExpectEqual ? Lhs == Rhs : Lhs != Rhs; \
    if (!Cond) { \
        WebTestRunnerSetFail( \
                Runner, \
                SourceInfo, \
                "Expected values of type '%s' to be %s, got\r\n\tlhs(%s) = " Fmt "\r\n\trhs(%s) = " Fmt, \
                #Type, \
                ExpectEqual ? "equal" : "different", \
                LhsRepr, \
                Lhs, \
                RhsRepr, \
                Rhs \
        ); \
        return 0; \
    } \
    return 1; \
}

_WEB_T_EQUAL_INT_GEN_IMPL(u8, "%hhu")
_WEB_T_EQUAL_INT_GEN_IMPL(u16, "%hu")
_WEB_T_EQUAL_INT_GEN_IMPL(u32, "%u")
_WEB_T_EQUAL_INT_GEN_IMPL(u64, "%lu")

_WEB_T_EQUAL_INT_GEN_IMPL(s8, "%hhd")
_WEB_T_EQUAL_INT_GEN_IMPL(s16, "%hd")
_WEB_T_EQUAL_INT_GEN_IMPL(s32, "%d")
_WEB_T_EQUAL_INT_GEN_IMPL(s64, "%ld")

#define _WEB_T_EQUAL_GENERIC_GEN_IMPL(Type, Fmt, FmtArg, Eq) static inline b32 _WEB_T_EQUAL_HANDLER_NAME(Type)(web_test_runner *Runner, Type Lhs, const char *LhsRepr, Type Rhs, const char *RhsRepr, web_source_info SourceInfo, b32 ExpectEqual) { \
    if (!!(Eq)((Lhs), (Rhs)) != !!ExpectEqual) { \
        WebTestRunnerSetFail( \
                Runner, \
                SourceInfo, \
                "Expected values of type '%s' to be %s, got\r\n\tlhs(%s) = " Fmt "\r\n\trhs(%s) = " Fmt, \
                #Type, \
                ExpectEqual ? "equal" : "different", \
                LhsRepr, \
                FmtArg(Lhs), \
                RhsRepr, \
                FmtArg(Rhs) \
        ); \
        return 0; \
    } \
    return 1; \
}

_WEB_T_EQUAL_GENERIC_GEN_IMPL(web_string_view, WEB_SV_FMT, WEB_SV_ARG, WebStringViewEqual)

#define _WEB_T_EQUAL_F(Lhs, Rhs, Equal) _Generic((Lhs), \
                              u8:  _WEB_T_EQUAL_HANDLER_NAME(u8), \
                              u16: _WEB_T_EQUAL_HANDLER_NAME(u16), \
                              u32: _WEB_T_EQUAL_HANDLER_NAME(u32), \
                              u64: _WEB_T_EQUAL_HANDLER_NAME(u64), \
                              s8:  _WEB_T_EQUAL_HANDLER_NAME(s8), \
                              s16: _WEB_T_EQUAL_HANDLER_NAME(s16), \
                              s32: _WEB_T_EQUAL_HANDLER_NAME(s32), \
                              s64: _WEB_T_EQUAL_HANDLER_NAME(s64), \
                              web_string_view: _WEB_T_EQUAL_HANDLER_NAME(web_string_view) \
        )(Runner, (Lhs), #Lhs, (Rhs), #Rhs, WEB_SOURCE_INFO_GET(), Equal)

#define WEB_T_EQUAL(Lhs, Rhs) do { \
    if (!_WEB_T_EQUAL_F(Lhs, Rhs, 1)) { \
        return; \
    } \
} while (0)

#define WEB_T_NEQUAL(Lhs, Rhs) do { \
    if (!_WEB_T_EQUAL_F(Lhs, Rhs, 0)) { \
        return; \
    } \
} while (0)

#define _WEB_T_BOOL_IMPL(Expected, Got) do { \
    b32 ExpectedX = (Expected); \
    b32 GotX = (Got); \
    if (ExpectedX != GotX) { \
        WebTestRunnerSetFail( \
                Runner, \
                WEB_SOURCE_INFO_GET(), \
                "Expected '%s' to be %s", \
                #Got, \
                ExpectedX ? "true" : "false" \
        ); \
        return; \
    } \
} while (0)

#define WEB_T_TRUE(X)  _WEB_T_BOOL_IMPL(1, (X))
#define WEB_T_FALSE(X) _WEB_T_BOOL_IMPL(0, (X))

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // WEB_TEST_H_
