#!/bin/sh

set -e

FLAGS="-g -Wall -Wextra -Werror -pedantic -Og -fpic"
BUILDTYPE=static

while getopts "de" flag; do
    case $flag in
        d)
            BUILDTYPE=dynamic
        ;;
        e)
            FLAGS=$FLAGS" -DWEB_USE_HTTPS_OPENSSL -lssl -lcrypto"
        ;;
        \?)
            echo "Unrecognized flag '$flag'"
        ;;
    esac
done

if [ "$BUILDTYPE" = "dynamic" ]; then
    FLAGS=$FLAGS" -shared -olibweb.so"
else
    FLAGS=$FLAGS" -c"
fi

cc $FLAGS src/http.c src/json.c src/common.c src/base64.c src/threadpool.c

if [ "$BUILDTYPE" = "static" ]; then
    ar rcs libweb.a http.o json.o common.o base64.o threadpool.o
fi
