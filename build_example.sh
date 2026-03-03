#!/bin/sh

set -xe

cc -Wall -Wextra -Werror -g -Og -o "$1" "./examples/$1.c" -L. -lweb -lssl -lcrypto -Wl,-rpath=$(pwd)
