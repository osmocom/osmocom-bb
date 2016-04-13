#!/usr/bin/env bash

autoreconf --install --force
./configure --enable-static
$MAKE $PARALLEL_MAKE
$MAKE distcheck
