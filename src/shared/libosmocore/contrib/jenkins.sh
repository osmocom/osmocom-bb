#!/usr/bin/env bash

set -ex

autoreconf --install --force
./configure --enable-static --enable-sanitize
$MAKE $PARALLEL_MAKE check \
  || cat-testlogs.sh
$MAKE distcheck \
  || cat-testlogs.sh
