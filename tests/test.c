#include <dlfcn.h>
#include <stdlib.h>
#include "../src/common.h"
#include "../src/test.h"
#include "../src/log.h"

typedef struct {
    void **Items;
    sz Capacity;
    sz Count;
} test_handlers_list;

void *GetSymbol(void *Handle, web_string_view Name) {
    web_temp Temp = WebGetTempArena();

    const char *NameCStr = WebStringViewCloneCStr(&Temp.Arena, Name);
    void *Symbol = dlsym(Handle, NameCStr);

    if (Symbol == NULL) {
        WEB_PANIC_FMT("failed to load symbol '%s' from the library: %s", NameCStr, dlerror());
    }

    WebReturnTempArena(Temp);

    return Symbol;
}

int main(int ArgumentsCount, char **Arguments) {
    ArgumentsCount--;
    Arguments++;

    if (ArgumentsCount == 0) {
        WEB_PANIC("no arguments provided");
    }

    s32 ExpectedArgs = 1;

    if (ArgumentsCount > ExpectedArgs) {
        WEB_PANIC_FMT("got %d args, expected %d", ArgumentsCount, ExpectedArgs);
    }

    WebLogSetDestination(stderr);

    web_string_view HandlerListString = WEB_SV_LIT(Arguments[0]);

    void *Handle = dlopen("./libweb.so", RTLD_LAZY);
    if (Handle == NULL) {
        WEB_PANIC_FMT("failed to open the shared library: %s", dlerror());
    }

    web_arena Arena = {0};
    WebArenaInitFixed(&Arena, 1024 * 1024);

    test_handlers_list Handlers;
    WEB_ARRAY_INIT(&Arena, &Handlers);

    web_string_view SymbolDelimiter = WEB_SV_LIT(",");

    while (HandlerListString.Count > 0) {
        web_string_view HandlerName = WebStringViewChop(HandlerListString, SymbolDelimiter);
        if (HandlerName.Count != 0) {
            void *Symbol = GetSymbol(Handle, HandlerName);
            WEB_ARRAY_PUSH(&Arena, &Handlers, Symbol);
        }

        HandlerListString.Items += HandlerName.Count + SymbolDelimiter.Count;
        HandlerListString.Count -= HandlerName.Count + SymbolDelimiter.Count;
    }

    WEB_LOG_FMT(INFO, TEST, "Found %ld test handlers", Handlers.Count);

    void (*SetupFn)(void) = GetSymbol(Handle, WEB_SV_LIT("WebTestInitRuntime"));

    SetupFn();

    b32 AllOk = 1;

    for (sz HandlerIdx = 0; HandlerIdx < Handlers.Count; ++HandlerIdx) {
        WEB_TEST_HANDLER_SIG((*HandlerF)) = Handlers.Items[HandlerIdx];
        b32 Ok = HandlerF();
        if (!Ok) AllOk = 0;
    }

    return !AllOk;
}
