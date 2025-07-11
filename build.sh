#!/bin/sh

set -xe

cc -Wall -Wextra -Werror -pedantic -Og -c -fpic src/http.c src/json.c src/common.c
ar rcs libweb.a http.o json.o common.o

