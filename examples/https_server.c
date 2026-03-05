#define WEB_USE_HTTPS_OPENSSL
#include "../src/http.h"
#include "../src/log.h"

web_http_response_status HelloHandler(web_http_response_context *Ctx) {
    WebHttpResponseWrite(Ctx, WEB_SV_LIT("Hello!"));
    return HTTP_STATUS_OK;
}

int main(int ArgsCount, char **Args) {
    ArgsCount--;
    Args++;

    enum {
        CertFlagIndex = 0,
        KeyFlagIndex = 1,
    };

    const char *Flags[] = {
        [CertFlagIndex] = "-cert",
        [KeyFlagIndex] = "-key",
    };
#define FLAGS_COUNT WEB_ARRAY_COUNT(Flags)
    char *FlagsValues[FLAGS_COUNT] = {0};
    uz FlagsValuesCount = 0;

    for (int ArgIndex = 0; ArgIndex < ArgsCount; ++ArgIndex) {
        char *Arg = Args[ArgIndex];

        for (uz FlagIndex = 0; FlagIndex < FLAGS_COUNT; ++FlagIndex) {
            const char *Flag = Flags[FlagIndex];

            if (strcmp(Arg, Flag) == 0) {
                ++ArgIndex;
                if (ArgIndex >= ArgsCount) {
                    fprintf(stderr, "Flag '%s' requires a value", Flag);
                    return 1;
                }

                char *FlagValue = Args[ArgIndex];
                FlagsValues[FlagIndex] = FlagValue;
                ++FlagsValuesCount;
            }
        }
    }

    if (FlagsValuesCount != FLAGS_COUNT) {
        fprintf(stderr, "You need to supply all the following flags:\n");
        for (uz I = 0; I < FLAGS_COUNT; ++I) {
            fprintf(stderr, "\t%s\n", Flags[I]);
        }
        return 1;
    }

    const char *CertFileName = FlagsValues[CertFlagIndex];
    const char *KeyFileName = FlagsValues[KeyFlagIndex];

    WebLogSetDestination(stdout);

    web_http_server Server = {0};
    web_https_openssl_provider_config OpenSSLConfig = {
        .CertificateFileName = CertFileName,
        .PrivateKeyFileName = KeyFileName,
    };
    web_https_provider Provider = {
        .Type = WEB_HTTPS_PROVIDER_OPENSSL,
        .Data = &OpenSSLConfig,
    };
    web_http_server_config Config = {
        .UseHttps = 1,
        .HttpsProvider = &Provider,
    };

    WEB_VERIFY(WebHttpServerInit(&Server, &Config));

    WebHttpServerAttachHandler(&Server, "/hello", HelloHandler);

    u16 Port = 6969;

    WebHttpServerStart(&Server, Port);

    return 0;
}
