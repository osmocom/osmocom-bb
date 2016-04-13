#!/usr/bin/env bash

set -ex

autoreconf --install --force
./configure --enable-static
$MAKE $PARALLEL_MAKE
$MAKE distcheck
