#!/bin/bash

set -xe

FLAGS="-g -Wall -Wextra -Werror -fpic"
BUILDTYPE=static
OPT=debug
SOURCES="src/http.c src/json.c src/common.c src/base64.c src/threadpool.c src/log.c src/pool.c src/utf.c"

while getopts "dezr" flag; do
    case $flag in
        d)
            BUILDTYPE=dynamic
        ;;
        e)
            FLAGS=$FLAGS" -DWEB_USE_HTTPS_OPENSSL $(pkg-config --cflags --libs openssl)"
            SOURCES=$SOURCES" src/openssl.c"
        ;;
        z)
            FLAGS=$FLAGS" -DWEB_MEMORY_SANITIZER -fno-omit-frame-pointer -fsanitize=address"
        ;;
        r)
            OPT=release
        ;;
        \?)
            echo "Unrecognized flag '$flag'"
            exit 1
        ;;
    esac
done

if [ "$BUILDTYPE" = "dynamic" ]; then
    FLAGS=$FLAGS" -shared -olibweb.so"
else
    FLAGS=$FLAGS" -c"
fi

if [ "$OPT" = "debug" ]; then
    FLAGS=$FLAGS" -Og"
else
    FLAGS=$FLAGS" -O2"
fi

cc $FLAGS $SOURCES

if [ "$BUILDTYPE" = "static" ]; then
    OBJS="${SOURCES//\.c/.o}"
    OBJS="${OBJS//src\//}"
    ar rcs libweb.a $OBJS
fi
