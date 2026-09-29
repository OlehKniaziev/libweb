#!/bin/sh

set -xe

cc -Wall -Wextra -Werror -g -Og -o "$1" "./examples/$1.c" -fno-omit-frame-pointer -L. -lasan -lweb -lssl -lcrypto -Wl,-rpath=$(pwd)
