#!/bin/sh

set -xe

./build.sh -e
cc -ggdb -O0 -Wall -Wextra -Werror -pedantic -o test tests/test.c -lweb -L. $(pkg-config --cflags --libs openssl)
./test
