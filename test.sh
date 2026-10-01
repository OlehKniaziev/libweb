#!/bin/sh

set -xe

./build.sh -de
./build.sh -e
TEST_CASES=`nm -jg libweb.so | grep --color=never '^WEB_TEST_CASE.*_RUN$' | tr '\n' ','`

cc -ggdb -O0 -Wall -Wextra -Werror -o test tests/test.c -L. -l:libweb.a
./test $TEST_CASES
