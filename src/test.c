#include "test.h"
#include "log.h"

void WebTestInitRuntime(void) {
    WebLogSetDestination(stderr);
    WebLogSetIncludeSource(0);
}

void WebTestRunnerInit(web_test_runner *Runner) {
    memset(Runner, 0, sizeof(*Runner));
    WebArenaInitChained(&Runner->Arena, 1024 * 32);
}

void WebTestRunnerExecute(
        web_test_runner *Runner,
        web_string_view TestCaseName,
        web_test_runner_proc Proc
) {
    Runner->CurrentTestCaseName = TestCaseName;
    WEB_LOG_FMT(INFO, TEST, "Executing test case " WEB_SV_FMT "...", WEB_SV_ARG(TestCaseName));
    Proc(Runner);
}

b32 WebTestRunnerReport(web_test_runner *Runner) {
    if (!Runner->Failed) {
        WEB_LOG_FMT(INFO, TEST, "Test case " WEB_SV_FMT " passed", WEB_SV_ARG(Runner->CurrentTestCaseName));
        return 1;
    } else {
        WEB_LOG_FMT(
                INFO,
                TEST,
                "Test case " WEB_SV_FMT " failed:\r\n%s:%d: " WEB_SV_FMT,
                WEB_SV_ARG(Runner->CurrentTestCaseName),
                Runner->CurrentSource.FileName,
                Runner->CurrentSource.Line,
                WEB_SV_ARG(Runner->FailMessage)
        );
        return 0;
    }
}

void WebTestRunnerSetFail(web_test_runner *Runner, web_source_info SourceInfo, const char *Fmt, ...) {
    WEB_ASSERT(!Runner->Failed);

    Runner->Failed = 1;
    Runner->CurrentSource = SourceInfo;

    va_list Args;

    va_start(Args, Fmt);
    s32 MessageCount = vsnprintf(NULL, 0, Fmt, Args);
    va_end(Args);

    u8 *Buffer = WebArenaPush(&Runner->Arena, MessageCount + 1, 1);

    va_start(Args, Fmt);
    vsnprintf((char *)Buffer, MessageCount + 1, Fmt, Args);
    va_end(Args);

    Runner->FailMessage.Items = Buffer;
    Runner->FailMessage.Count = MessageCount;
}
