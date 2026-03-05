#!/bin/sh

set -e

FLAGS="-g -Wall -Wextra -Werror -Og -fpic"
BUILDTYPE=static
SOURCES="src/http.c src/json.c src/common.c src/base64.c src/threadpool.c src/log.c"

while getopts "de" flag; do
    case $flag in
        d)
            BUILDTYPE=dynamic
        ;;
        e)
            FLAGS=$FLAGS" -DWEB_USE_HTTPS_OPENSSL -lssl -lcrypto"
            SOURCES=$SOURCES" src/openssl.c"
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

cc $FLAGS $SOURCES

if [ "$BUILDTYPE" = "static" ]; then
    OBJS="${SOURCES//\.c/.o}"
    OBJS="${OBJS//src\//}"
    ar rcs libweb.a $OBJS
fi
