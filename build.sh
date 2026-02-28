#!/bin/sh

set -xe

FLAGS="-g -Wall -Wextra -Werror -pedantic -Og -c -fpic"

if [ "$1" = "https" ]; then
    FLAGS=$FLAGS" -DWEB_USE_HTTPS"
elif [ "$1" = "openssl" ]; then
    FLAGS=$FLAGS" -DWEB_USE_HTTPS -DWEB_USE_HTTPS_OPENSSL"
fi

cc $FLAGS src/http.c src/json.c src/common.c src/base64.c src/threadpool.c
ar rcs libweb.a http.o json.o common.o base64.o threadpool.o
