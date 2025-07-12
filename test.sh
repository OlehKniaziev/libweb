#!/bin/sh

set -xe

cc -ggdb -O0 -Wall -Wextra -Werror -pedantic -o test tests/test.c -lweb -L.
./test
