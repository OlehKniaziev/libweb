#!/bin/sh

set -xe

cc -g -Wall -Wextra -Werror -pedantic -Og -c -fpic src/http.c src/json.c src/common.c src/base64.c src/threadpool.c
ar rcs libweb.a http.o json.o common.o base64.o threadpool.o
