#!/bin/sh

set -xe

cc -c -fpic src/http.c src/json.c
ar rcs libweb.a http.o json.o
